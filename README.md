# ffplay_vc

# Brief
    Offten, we may need embed an  'AV decoder/player' into our application.
    At that point, 'ffplay' would be a good start, em... Until we looked at the code :) Yes 'ffplay' is a comprehensive sample of ffmpeg, it is not tend to be reusale.
    We need a 'clean' ffplay, yes here it  is.

# Build
```
on Windows:
    No extra dependencies is reqired, it carries a set of ffmpeg+SDL2 under the dir 'dep_on_windows'.
    Just open the .sln with VC2017 or higher and build/run.
```
```
on Linux:
    At this time, I use system installed SDL2-devel, and refer my local source built ffmpeg. You may want to tweak the 'Makefile' to set FFCFLAGS/FFLDFLAGS match your evn.
    Then just 'make', 'make test' or 'make valgrind'
```

# TODO:

For linux version, I guess so be it, the purpose of linux version is simplely 'to check my code by valgrind'. :)

On windows, the final object is to shape 'ffplay_vc' to a real windows app( no SDL) and the interface between UI <=> decoder should be simple and clean.


    

