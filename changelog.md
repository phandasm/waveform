- Fix crash when using audio configurations with more than 2 channels
- Fix name conflict with other plugins
- Add bar and "stepped" bar visualizers
- Minor performance improvements

# **IMPORTANT**
Due to being renamed to avoid conflicts with other plugins, *sources made with version 1.0.0 will no longer be recognized.*  
If you would like to preserve those sources you can edit your scene files in `%appdata%/obs-studio/basic/scenes` and replace occurances of `waveform_source` with `phandasm_waveform_source`.  
Apologies for the inconvenience.

## Installation
### Windows
Either  
- Use the installer and select your OBS folder.  
or  
- Extract Waveform\_v#.#.#\_x86\_64.zip to the *root* of your OBS folder (e.g. `C:\Program Files (x86)\obs-studio`).

#### Requirements (included with installer)
- [Visual Studio 2019 Redistributable](https://docs.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)

### Linux (Ubuntu 20.04)
#### Prebuilt Binaries
- Extract Waveform\_v#.#.#\_Ubuntu\_x64.tar.gz to your `~/.config/obs-studio/plugins` folder.  

#### Source Build
- Step-by-step instructions in the [readme](https://github.com/phandasm/waveform/blob/master/README.md#linux-ubuntu-20043-lts).
