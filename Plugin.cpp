#include "Engine.h"
#include <atomic>
#include <optional>
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
const FUID processorId(0x328197AC, 0x25624C79, 0x913BB334, 0x4525A66B);
const FUID controllerId(0x8727916D, 0x139C4E88, 0x877A1769, 0x13C5BB16);
using Values = decltype(anks::defaults)::value_type;
using Settings = std::array<Values, anks::Count>;

bool readState(IBStream* stream, Settings& settings) {
    if (!stream)
        return false;
    IBStreamer reader(stream, kLittleEndian);
    int32 version{};
    Settings loaded = anks::defaults;
    if (!reader.readInt32(version) || (version != 1 && version != 2))
        return false;
    const std::size_t count = version == 1 ? anks::Repeat : anks::Count;
    for (std::size_t id = 0; id < count; ++id) {
        auto& value = loaded[id];
        if (!reader.readDouble(value) || !std::isfinite(value) || value < 0. || value > 1.)
            return false;
    }
    // This legacy setting is superseded by the host's device switch.
    loaded[anks::Enable] = 1.;
    settings = loaded;
    return true;
}


// Each snapshot has one writer. Atomic payloads keep a rejected read race-free.
class StateSnapshot {
public:
    StateSnapshot() {
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i].store(anks::defaults[i]);
    }

    void publish(const Settings& next) {
        const auto revision = sequence.load();
        sequence.store(revision + 1);
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i].store(next[i]);
        sequence.store(revision + 2);
    }

    bool read(Settings& result, uint64& revision) const {
        revision = sequence.load();
        if (revision % 2 != 0)
            return false;
        for (std::size_t i = 0; i < values.size(); ++i)
            result[i] = values[i].load();
        return revision == sequence.load();
    }

private:
    static_assert(std::atomic<double>::is_always_lock_free &&
                  std::atomic<uint64>::is_always_lock_free);
    std::array<std::atomic<double>, anks::Count> values{};
    std::atomic<uint64> sequence{};
};

struct Automation {
    IParamValueQueue* queue{};
    int32 index{}, offset{};
    ParamValue value{};
    bool pending{};

    void next() {
        pending = index < queue->getPointCount() &&
                  queue->getPoint(index++, offset, value) == kResultTrue;
    }
};

class Processor final : public AudioEffect {
public:
    Processor() {
        setControllerClass(controllerId);
        processContextRequirements.needTempo()
            .needProjectTimeMusic().needContinousTimeSamples().needTransportState();
    }

    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new Processor); }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto status = AudioEffect::initialize(context);
        if (status != kResultOk)
            return status;
        addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        return kResultOk;
    }

    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                        SpeakerArrangement* outputs, int32 numOuts) override {
        if (numIns != 1 || numOuts != 1 || inputs[0] != SpeakerArr::kStereo ||
            outputs[0] != SpeakerArr::kStereo)
            return kResultFalse;
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
        if (!std::isfinite(setup.sampleRate) || setup.sampleRate <= 0. || setup.sampleRate > 2000000.)
            return kInvalidArgument;
        try {
            engine.prepare(setup.sampleRate);
        } catch (const std::bad_alloc&) {
            return kOutOfMemory;
        }
        localBeat = 0.;
        expectedPosition.reset();
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override {
        engine.reset();
        localBeat = 0.;
        expectedPosition.reset();
        return AudioEffect::setActive(state);
    }

    tresult PLUGIN_API setState(IBStream* stream) override {
        Settings restored{};
        if (!readState(stream, restored))
            return kResultFalse;
        pendingState.publish(restored);
        return kResultTrue;
    }

    tresult PLUGIN_API getState(IBStream* stream) override {
        if (!stream)
            return kInvalidArgument;
        IBStreamer writer(stream, kLittleEndian);
        if (!writer.writeInt32(2))
            return kResultFalse;
        Settings snapshot{};
        uint64 revision{};
        if (!pendingState.read(snapshot, revision))
            return kResultFalse;
        if (revision == appliedState.load()) {
            while (!processedState.read(snapshot, revision)) {
                // State is requested on the UI thread, never from the audio callback.
            }
        }
        for (auto value : snapshot)
            if (!writer.writeDouble(value))
                return kResultFalse;
        return kResultTrue;
    }

    tresult PLUGIN_API process(ProcessData& data) override {
        Settings restored{};
        uint64 revision{};
        const bool stateChanged = pendingState.read(restored, revision) && revision != audioRevision;
        if (stateChanged) {
            settings = restored;
            audioRevision = revision;
            engine.reset();
        }
        bool changed = stateChanged;
        std::array<Automation, anks::Count> automation{};
        if (auto* changes = data.inputParameterChanges) {
            for (int32 i = 0; i < changes->getParameterCount(); ++i) {
                auto* queue = changes->getParameterData(i);
                if (queue && queue->getParameterId() < settings.size() &&
                    queue->getParameterId() != anks::Enable) {
                    auto& cursor = automation[queue->getParameterId()];
                    cursor.queue = queue;
                    cursor.next();
                }
            }
        }

        const auto* host = data.processContext;
        const bool playing = !host || (host->state & ProcessContext::kPlaying);
        const double tempo = host && (host->state & ProcessContext::kTempoValid) &&
                             host->tempo >= 20. && host->tempo <= 400. ? host->tempo : 120.;
        const bool anchored = host && (host->state & ProcessContext::kProjectTimeMusicValid)
                              && std::isfinite(host->projectTimeMusic);
        const double step = tempo / (60. * processSetup.sampleRate);
        const double beat = anchored ? host->projectTimeMusic : localBeat;
        const bool hasAudio = data.numSamples > 0 && data.numInputs && data.numOutputs &&
                              data.inputs[0].numChannels == 2 && data.outputs[0].numChannels == 2;

        if (hasAudio && host && playing && expectedPosition &&
            host->projectTimeSamples != *expectedPosition)
            engine.reset();
        if (hasAudio) {
            expectedPosition.reset();
            if (host && playing)
                expectedPosition = host->projectTimeSamples + data.numSamples;
        }

        float inputGain = gain(settings[anks::InDb]);
        float outputGain = gain(settings[anks::OutDb]);
        uint64 silent = 3;
        for (int32 i = 0; i < std::max(1, data.numSamples); ++i) {
            for (std::size_t id = 0; id < settings.size(); ++id) {
                auto& cursor = automation[id];
                while (cursor.pending && (cursor.offset <= i || !hasAudio)) {
                    if (std::isfinite(cursor.value)) {
                        changed = true;
                        settings[id] = std::clamp(cursor.value, 0., 1.);
                        if (id == anks::InDb)
                            inputGain = gain(settings[id]);
                        if (id == anks::OutDb)
                            outputGain = gain(settings[id]);
                    }
                    cursor.next();
                }
            }
            if (!hasAudio)
                break;

            auto** input = data.inputs[0].channelBuffers32;
            auto** output = data.outputs[0].channelBuffers32;
            const auto frame = engine.process({input[0][i], input[1][i]}, beat + i * step,
                tempo, playing, anks::divisions[anks::divisionIndex(settings[anks::Stutter])],
                anks::divisions[anks::divisionIndex(settings[anks::Refresh])],
                inputGain, outputGain, settings[anks::Mix], settings[anks::Enable] >= .5,
                settings[anks::Bypass] >= .5, settings[anks::Repeat] >= .5);
            for (int channel = 0; channel < 2; ++channel) {
                output[channel][i] = frame[channel];
                if (frame[channel] != 0.)
                    silent &= ~(uint64{1} << channel);
            }
        }
        if (hasAudio) {
            data.outputs[0].silenceFlags = silent;
            localBeat = beat + (playing ? data.numSamples * step : 0.);
        }
        if (changed) {
            processedState.publish(settings);
            appliedState.store(audioRevision);
        }
        return kResultOk;
    }

private:
    static float gain(double value) {
        return static_cast<float>(std::pow(10., (-24. + value * 36.) / 20.));
    }
    Settings settings = anks::defaults;
    StateSnapshot pendingState, processedState;
    std::atomic<uint64> appliedState{};
    uint64 audioRevision{};
    anks::Engine engine;
    double localBeat{};
    std::optional<TSamples> expectedPosition;
};

class Controller final : public EditController {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new Controller); }

    tresult PLUGIN_API initialize(FUnknown* context) override {
        const auto status = EditController::initialize(context);
        if (status != kResultOk)
            return status;
        constexpr auto automate = ParameterInfo::kCanAutomate;
        addToggle(STR16("Enable"), anks::Enable,
                  ParameterInfo::kIsHidden | ParameterInfo::kIsReadOnly);
        parameters.addParameter(new RangeParameter(STR16("Input"), anks::InDb,
                                                  STR16("dB"), -24., 12., 0.));
        parameters.addParameter(new RangeParameter(STR16("Output"), anks::OutDb,
                                                  STR16("dB"), -24., 12., 0.));
        addDivision(STR16("Stutter"), anks::Stutter);
        addDivision(STR16("Refresh"), anks::Refresh);
        parameters.addParameter(new RangeParameter(STR16("Mix"), anks::Mix,
                                                  STR16("%"), 0., 100., 100.));
        addToggle(STR16("Repeat"), anks::Repeat, automate);
        addToggle(STR16("Bypass"), anks::Bypass, automate | ParameterInfo::kIsBypass);
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* stream) override {
        Settings settings{};
        if (!readState(stream, settings))
            return kResultFalse;
        for (ParamID id = 0; id < settings.size(); ++id)
            setParamNormalized(id, settings[id]);
        return kResultTrue;
    }

private:
    void addToggle(const TChar* title, anks::Parameter id, int32 flags) {
        auto* parameter = new StringListParameter(title, id, nullptr, flags);
        parameter->appendString(STR16("Off"));
        parameter->appendString(STR16("On"));
        setDefault(parameter, id);
    }

    void addDivision(const TChar* title, anks::Parameter id) {
        auto* parameter = new StringListParameter(title, id);
        for (const auto* label : {STR16("1/1"), STR16("1/2"), STR16("1/4"),
                                 STR16("1/8"), STR16("1/16"), STR16("1/32"), STR16("1/64")})
            parameter->appendString(label);
        setDefault(parameter, id);
    }

    void setDefault(Parameter* parameter, anks::Parameter id) {
        parameter->getInfo().defaultNormalizedValue = anks::defaults[id];
        parameter->setNormalized(anks::defaults[id]);
        parameters.addParameter(parameter);
    }
};
}

BEGIN_FACTORY_DEF("ANKS Labs", "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processorId), PClassInfo::kManyInstances,
           kVstAudioEffectClass, "ANKS Fragment Engine", Vst::kDistributable,
           "Fx|Delay", "0.2.0", kVstVersionString, Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controllerId), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "ANKS Fragment Engine Controller", 0,
           "", "0.2.0", kVstVersionString, Controller::create)
END_FACTORY
