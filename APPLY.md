# Palettes step 5 — drop 4 (ALL FOUR UNITS)

Every file here is COMPLETE. Overwrite, do not edit. No hand-editing of
CMakeLists.txt — the copy in this folder is the finished one.

    cd ~/Documents/PZMapMaker
    cp /path/to/download/{CMakeLists.txt,groundmaterial.hpp,groundmaterial.cpp,maskrule.hpp,maskrule.cpp,maskrule_selftest.cpp,groundpalette.hpp,groundpalette.cpp,tilepalette.hpp,tilepalette.cpp,palettes_oracle.cpp,PalettesOracle.java} .
    cp PalettesOracle.java ~/Documents/PZMapCreation/src/main/java/pzformat/

## Confirm you have the NEW files, not the previous drop

    wc -l ~/Documents/PZMapMaker/palettes_oracle.cpp ~/Documents/PZMapMaker/PalettesOracle.java

Must be **532** and **658**. Anything else means the copy did not overwrite,
and the digest will come out short.

All twelve files, expected line counts:

    CMakeLists.txt          125
    groundmaterial.hpp      123    groundmaterial.cpp       61
    maskrule.hpp            185    maskrule.cpp             80
    maskrule_selftest.cpp   192
    groundpalette.hpp       133    groundpalette.cpp       141
    tilepalette.hpp         132    tilepalette.cpp         307
    palettes_oracle.cpp     532    PalettesOracle.java     658

## TOUCH FIRST — files from a session land ~4 hours in the future

Containers stamp mtimes in UTC; this machine is EDT. Ninja then fails with
`manifest 'build.ninja' still dirty` AFTER printing `Configuring done`, builds
nothing, and leaves the OLD binary in place. `CMakeLists.txt` is the one most
often missed, because `touch *.cpp *.hpp` does not match it.

    touch ~/Documents/PZMapMaker/*.cpp ~/Documents/PZMapMaker/*.hpp \
          ~/Documents/PZMapMaker/CMakeLists.txt \
          ~/Documents/PZMapCreation/src/main/java/pzformat/PalettesOracle.java

## Build and verify

    cmake -S ~/Documents/PZMapMaker -B /tmp/noqt -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
    cmake --build /tmp/noqt --target pz_palettes_oracle pz_maskrule_selftest

    cd ~/Documents/PZMapCreation
    javac -d /tmp/out src/main/java/pzformat/MaskRule.java
    java -cp /tmp/out pzformat.MaskRule > /tmp/mr.java.txt; echo "java exit $status"
    /tmp/noqt/pz_maskrule_selftest      > /tmp/mr.cpp.txt;  echo "cpp  exit $status"
    diff /tmp/mr.java.txt /tmp/mr.cpp.txt; and echo SELFTEST-IDENTICAL

    javac -d /tmp/jo -sourcepath src/main/java src/main/java/pzformat/PalettesOracle.java
    java -cp /tmp/jo pzformat.PalettesOracle /tmp/p.java.txt 5000 | tail -1
    /tmp/noqt/pz_palettes_oracle /tmp/p.cpp.txt 5000 | tail -1
    cmp /tmp/p.java.txt /tmp/p.cpp.txt; and echo BYTE-IDENTICAL
    md5sum /tmp/p.java.txt

## Predictions

- Self-test: 15 lines both sides, `9 / 9 cases pass`, exit 0 both sides.
- Digest: **345,732 lines, 14,978,074 bytes,
  md5 `f4ed4cb4acbc7440280bc8e19b0f49bd`.**
  254,354 means the old oracle files are still in place.
- ~87 lines of `ground palette: dropped N unusable base tiles` on stdout from
  BOTH sides before the summary. That is `pick()`'s real behaviour, reproduced
  deliberately; it is not part of the digest.
- Zero warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast`.
