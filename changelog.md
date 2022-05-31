- Add native support for Arm64 (aka 'Apple Silicon') based Macs
- Add Simplified Chinese localization (#14 thanks to [神枪968](https://github.com/GodGun968))
- Add option to roll-off the edges of the graph (#12)

## Installation
Note that on x86 Waveform requires an AVX capable CPU (all Intel and AMD since ~2011).

### Windows
Either  
- Use the installer and select your OBS folder.  
or  
- Extract Waveform\_v#.#.#\_x86\_64.zip to the *root* of your OBS folder (e.g. `C:\Program Files (x86)\obs-studio`).

Both methods include 32-bit and 64-bit binaries.

### Linux (Ubuntu 20.04, Flatpak)
<details>
<summary>Instructions</summary>

#### Prebuilt Binaries
- Extract Waveform\_v#.#.#\_Ubuntu\_x64.tar.gz to your `~/.config/obs-studio/plugins` folder.  

#### Flatpak
- `flatpak install flathub com.obsproject.Studio.Plugins.waveform`  

#### Source Build
- Step-by-step instructions in the [readme](https://github.com/phandasm/waveform/blob/master/README.md#linux-ubuntu-20043-lts).
</details>

### MacOS (10.13+)
<details>
<summary>Instructions</summary>

#### M1 (Arm) Macs
- Extract Waveform\_v#.#.#\_MacOS\_Arm64.zip to your `/Library/Application Support/obs-studio/plugins` folder.  

#### Intel Macs
- Extract Waveform\_v#.#.#\_MacOS\_x64.zip to your `/Library/Application Support/obs-studio/plugins` folder.  
</details>

### [OBS Music Edition 27.2.4 (x64)](https://github.com/pkviet/obs-studio/releases/tag/v27.2.4)
<details>
<summary>Instructions</summary>

- Extract for_OBS_ME_only.zip to the *root* of your OBS ME folder (e.g. `C:\Program Files\obs-studio-ME`).
</details>
