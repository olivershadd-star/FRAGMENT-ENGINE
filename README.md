<div align="center">

# ANKS LABS / FRAGMENT ENGINE
### RHYTHMIC STUTTER · REPEAT · PITCH PATTERNS

![Status](https://img.shields.io/badge/STATUS-PROTOTYPE_0.3.0-EF343E?style=for-the-badge&labelColor=10151E)
![Audio](https://img.shields.io/badge/AUDIO-DEVELOPMENT-19B9FF?style=for-the-badge&labelColor=10151E)

**BUILD. BREAK. REWIRE. LISTEN.**

[![ANKS LABS](https://img.shields.io/badge/ANKS_LABS-GITHUB_PROFILE-19B9FF?style=for-the-badge&labelColor=10151E)](https://github.com/olivershadd-star)

</div>

---

## 01 // LE PROJET

**FRAGMENT ENGINE** est un prototype d'effet audio VST3, avec cœur C++20 indépendant du SDK, pour capturer des fragments courts et les répéter sur une grille rythmique. Les modes Pattern et Step font évoluer la hauteur des répétitions et ouvrent des possibilités de sound design sur batterie, textures et voix.

> **Statut : prototype 0.3.0.** Le cœur et des tests Linux/VST3 sont documentés ; la validation du build Windows et dans Ableton demeure une étape séparée. Aucune version stable n'est annoncée.

**[Sources & documentation](https://github.com/olivershadd-star/FRAGMENT-ENGINE)** · **[Builds de test / GitHub Actions](https://github.com/olivershadd-star/FRAGMENT-ENGINE/actions)**

---

## Documentation technique existante — Fragment Engine 0.3.0


Prototype d’effet VST3 stéréo 32 bits flottants. Le moteur C++20 est indépendant du SDK. Le processeur Steinberg reçoit le transport et les événements d’automation. Le contrôleur expose les paramètres dans l’interface générique du DAW.

## Comportement

À chaque frontière REFRESH, le moteur laisse passer et capture le premier fragment STUTTER, puis le répète jusqu’à la prochaine frontière. À 120 BPM et 48 kHz, les valeurs initiales (STUTTER 1/16, REFRESH 1/4) donnent 6 000 échantillons de capture puis trois répétitions dans un cycle de 24 000 échantillons. REFRESH est limité par la durée de STUTTER lorsqu’il est réglé plus court. Le démarrage entre deux frontières attend la suivante. Un saut du transport invalide le fragment. Sans contexte de transport, le moteur utilise une horloge interne à 120 BPM.

Le lissage des raccords interpole depuis le dernier échantillon wet pendant au plus 2 ms. Les réglages sont sauvegardés avec une version de format. Les fragments restent en mémoire volatile. Le bypass VST3 restitue le signal original, et la latence déclarée est de zéro échantillon.

Utiliser l’interrupteur du périphérique dans Ableton pour activer ou désactiver l’effet. Les anciens états restent lisibles. Le réglage Enable de la version 0.1 et son automation sont ignorés.

Ce prototype couvre LIVE_CAPTURE, les divisions droites de 1/1 à 1/64, le réglage dry/wet, les gains et le bypass. Les signatures rythmiques complexes, les modes millisecondes et HISTORY_CAPTURE, Freeze, les effets supplémentaires et l’éditeur graphique sont hors de cette itération.

## Jouer une voix dans Ableton

Le paramètre **Repeat** est automatisable dans le périphérique. Off laisse passer la voix et le moteur continue à capter selon **Refresh**. On relance le fragment depuis son début s’il est déjà capturé. Si le fragment est en cours de capture, la répétition démarre à la fin de la capture. Les passages On et Off sont lissés. Les anciens projets chargent Repeat sur On.

Pour une voix posée sur le temps, essayer **Stutter 1/4**, **Refresh 1/2**, **Mix 100 %**. Place « la la la » sur le temps 1 et « dou dou dou » sur le temps 3. Active Repeat sur les temps 2 et 4 pour doubler les deux appels. Coupe Repeat sur le temps 4 pour laisser la fin originale. Choisis **Stutter 1/8** pour répéter un fragment plus court.

## Créer une mélodie à partir d’une nappe

Avec **Stutter 1/16**, **Refresh 1/4**, **Repeat On** et **Mix 100 %**, sélectionner **Pitch Mode → Pattern**. Régler **Pitch 1–4** sur **0, +3, +7, +3** en demi-tons. Le fragment initial passe à sa hauteur d’origine. Les trois répétitions prennent les valeurs Pitch 2, 3 et 4. Pour jouer Pitch 1 explicitement, couper puis réactiver Repeat après la capture. Après quatre pas, le motif boucle jusqu’au prochain Refresh.

En mode **Step**, **Pitch Step** indique l’écart entre deux répétitions. À **+2**, on obtient 0, +2, +4, +6. Les décalages sont limités à deux octaves et les paramètres sont automatisables dans Live. Un changement de pitch s’applique au début du fragment suivant. Les projets existants chargent en mode **Off**.

La transposition se fait par variation de la vitesse de lecture du fragment. Le départ et la durée de chaque répétition restent calés sur Stutter. Vers le haut, la lecture peut reboucler à l’intérieur d’une répétition. Vers le bas, elle lit une partie du fragment. Les raccords sont lissés et la transposition modifie aussi le timbre.

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
