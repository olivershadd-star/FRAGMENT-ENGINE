# ANKS Fragment Engine 0.1.1

Prototype d’effet VST3 stéréo 32 bits flottants. Le moteur C++20 est indépendant du SDK. Le processeur Steinberg reçoit le transport et les événements d’automation. Le contrôleur expose les paramètres dans l’interface générique du DAW.

## Comportement

À chaque frontière REFRESH, le moteur laisse passer et capture le premier fragment STUTTER, puis le répète jusqu’à la prochaine frontière. À 120 BPM et 48 kHz, les valeurs initiales (STUTTER 1/16, REFRESH 1/4) donnent 6 000 échantillons de capture puis trois répétitions dans un cycle de 24 000 échantillons. REFRESH est limité par la durée de STUTTER lorsqu’il est réglé plus court. Le démarrage entre deux frontières attend la suivante. Un saut du transport invalide le fragment. Sans contexte de transport, le moteur utilise une horloge interne à 120 BPM.

Le lissage des raccords interpole depuis le dernier échantillon wet pendant au plus 2 ms. Les réglages sont sauvegardés avec une version de format. Les fragments restent en mémoire volatile. Le bypass VST3 restitue le signal original, et la latence déclarée est de zéro échantillon.

Utiliser l’interrupteur du périphérique dans Ableton pour activer ou désactiver l’effet. Les anciens états restent lisibles. Le réglage Enable de la version 0.1 et son automation sont ignorés.

Ce prototype couvre LIVE_CAPTURE, les divisions droites de 1/1 à 1/64, le réglage dry/wet, les gains et le bypass. Les signatures rythmiques complexes, les modes millisecondes et HISTORY_CAPTURE, Freeze, les effets supplémentaires et l’éditeur graphique sont hors de cette itération.

## Compiler et lancer

CMake 3.25+, C++20 et Git. CMake récupère le SDK Steinberg 3.8.1 build 84 et les sous-modules nécessaires. Pour un SDK déjà extrait, ajouter `-DVST3_SDK_ROOT=/chemin/vers/vst3sdk`.

### Linux x86-64

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/anks_probe original.wav stutter.wav
```

Le bundle est généré sous `build/VST3/Release`. Lancer le validateur Steinberg avec le chemin du bundle, ou charger celui-ci dans un hôte VST3 Linux.

### Windows x64 avec Visual Studio 2022

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
```

Charger le bundle Windows dans VST3PluginTestHost ou Ableton Live après compilation. La version Linux et le validateur Steinberg ont servi à la validation de cette livraison.

### Moteur seul

Configurer avec `-DANKS_BUILD_VST3=OFF` pour compiler et lancer le test du moteur et produire les WAV sans dépendance Steinberg.
