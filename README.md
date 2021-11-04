# Waveform
Waveform is an audio spectral analysis plugin for [OBS Studio](https://obsproject.com/).  
It is based on [FFTW](https://www.fftw.org/) and optimized for AVX2/FMA3.  
![Screenshot](https://i.imgur.com/1nUHTyt.png)

# Compiling
Waveform's only external dependency is libObs.  
You'll need to build obs-studio separately and point [CMake](https://cmake.org/) to it when building Waveform.  
From the CLI interface, this can be accomplished via `-DLibObs_ROOT="path/to/obs-studio"`.
