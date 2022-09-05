# Mac and Linux users please uninstall older versions first
- Experimental support for OBS 28 on M1 Macs
- Add volume normalization option
- Rendering performance improvements (#24 thanks to Ori Sky)
- Fix typo resulting in slightly incorrect gaussian kernels

## Installation
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

Note: Use the ARM version only if you have a native ARM build of OBS.  
The ARM version requires OBS 28.0 or newer.
</details>

### [OBS Music Edition 27.2.4 (Windows x64)](https://github.com/pkviet/obs-studio/releases/tag/v27.2.4)
<details>
<summary>Click for instructions</summary>

- Extract for_OBS_ME_only.zip to the *root* of your OBS ME folder (e.g. `C:\Program Files\obs-studio-ME`).
</details>
