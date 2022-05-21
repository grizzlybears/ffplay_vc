# ffplay_vc

# Brief
    Offten, we hope we can embed an  'AV decoder/player' into our own application.
    At that point, 'ffplay' would be a good start, em... until we see the code :) Yes 'ffplay' is a comprehensive sample of ffmpeg, it doesn't intend to be reusable.
    We need a 'clean' ffplay, and yes here it is.

# Build
```
on Windows:
    No extra dependencies is required, the working copy carries a set of ffmpeg+SDL2 in 'dep_on_windows'.
    Just open the .sln with VC2017 or higher and build/run. There are two 'vcproj's, 'ffplay' is the 'classic' command line style 'ffplay', and 'ffplay_vc' is a windows GUI behaving as a simplest video player.
```

```
on Linux:
    At this time, I use system installed SDL2-devel, and refer my local source built ffmpeg. You may want to tweak the 'Makefile' to set FFCFLAGS/FFLDFLAGS match your environment.
    Then just 'make', 'make test' or 'make valgrind'
```

# TODO:

For linux version, I guess 'so be it', the purpose of linux version is simplely 'to check my code by valgrind'. :)

On windows, the final object is to shape 'ffplay_vc' to a real windows app, that is 'drawing pirctures by d3d and playing sound by XAudio', and the interface between UI <=> decoder should be simple and clean.


