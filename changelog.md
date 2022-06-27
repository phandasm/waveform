- Fix rendering artifacts when using curve graph
- Fix compatibility issues (rosetta 2, non-AVX CPUs)
- Update Chinese localization

<details>
<summary>Changes from 1.4.0</summary>

- Add native support for Arm64 (aka 'Apple Silicon') based Macs
- Add Simplified Chinese localization (#14 thanks to [神枪968](https://github.com/GodGun968))
- Add option to roll-off the edges of the graph (#12)
</details>

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
<summary>Click for instructions</summary>

#### Prebuilt Binaries
- Extract Waveform\_v#.#.#\_Ubuntu\_x64.tar.gz to your `~/.config/obs-studio/plugins` folder.  

#### Flatpak
- `flatpak install flathub com.obsproject.Studio.Plugins.waveform`  

#### Source Build
- Step-by-step instructions in the [readme](https://github.com/phandasm/waveform/blob/master/README.md#linux-ubuntu-20043-lts).
</details>

### MacOS (10.13+)
<details>
<summary>Click for instructions</summary>

- Extract Waveform\_v#.#.#\_MacOS\_x64.zip to your `/Library/Application Support/obs-studio/plugins` folder.  

Note: Even if you have an M1 Mac, you probably want the x64. Use the ARM version only if you have a native ARM build of OBS.  
If upgrading from a previous ARM build, please be sure to delete `libwaveform.so` if present.
</details>

### [OBS Music Edition 27.2.4 (Windows x64)](https://github.com/pkviet/obs-studio/releases/tag/v27.2.4)
<details>
<summary>Click for instructions</summary>

- Extract for_OBS_ME_only.zip to the *root* of your OBS ME folder (e.g. `C:\Program Files\obs-studio-ME`).
</details>
