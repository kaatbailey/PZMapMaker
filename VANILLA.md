# The vanilla-install leg — the last Definition-of-Done item

Everything verified so far is SYNTHETIC. This is the other half the chunk asks
for. Two files changed: `palettes_oracle.cpp` (651) and `PalettesOracle.java`
(776). Nothing else, and the synthetic digest md5 is unchanged.

## Copy and touch

    cp palettes_oracle.cpp PalettesOracle.java ~/Documents/PZMapMaker/
    cp PalettesOracle.java ~/Documents/PZMapCreation/src/main/java/pzformat/
    touch ~/Documents/PZMapMaker/palettes_oracle.cpp \
          ~/Documents/PZMapMaker/PalettesOracle.java \
          ~/Documents/PZMapCreation/src/main/java/pzformat/PalettesOracle.java
    wc -l ~/Documents/PZMapMaker/palettes_oracle.cpp \
          ~/Documents/PZMapMaker/PalettesOracle.java
    # expect 651 and 776

## Step 1 — does the install exist?

The chunk prompt says find this out in the first five minutes, not the last
hour. It has taken until now only because the synthetic half needed no files.

    set MEDIA ~/.local/share/Steam/steamapps/common/ProjectZomboid/media
    ls $MEDIA/texturepacks | wc -l
    ls $MEDIA/*.tiles | wc -l

If `$MEDIA` is wrong, find it and substitute:

    find ~ -maxdepth 8 -type d -name texturepacks 2>/dev/null

## Step 2 — confirm the synthetic digest still matches

The vanilla leg is a separate code path; this proves adding it changed nothing.

    cmake --build /tmp/noqt --target pz_palettes_oracle
    cd ~/Documents/PZMapCreation
    javac -d /tmp/jo -sourcepath src/main/java src/main/java/pzformat/PalettesOracle.java
    java -cp /tmp/jo pzformat.PalettesOracle /tmp/p.java.txt 5000 | tail -1
    /tmp/noqt/pz_palettes_oracle /tmp/p.cpp.txt 5000 | tail -1
    cmp /tmp/p.java.txt /tmp/p.cpp.txt; and echo SYNTHETIC-STILL-IDENTICAL

Expect 429,257 lines and md5 `d28516268cdaae203275e778b2176563`.

## Step 3 — the vanilla leg

A THIRD argument switches to vanilla-only output.

    java -cp /tmp/jo pzformat.PalettesOracle /tmp/van.java.txt 0 $MEDIA
    /tmp/noqt/pz_palettes_oracle /tmp/van.cpp.txt 0 $MEDIA
    cmp /tmp/van.java.txt /tmp/van.cpp.txt; and echo VANILLA-BYTE-IDENTICAL

Then, whatever the result, paste these — they are the interesting lines:

    command grep -e '^VIN' -e '^VGP\t' -e '^VGPSTR' -e '^VTP\t' \
                -e '^VTPVERIFY' -e '^VTPSKINN' -e '^VGM' /tmp/van.java.txt

## Predictions, written before the run

- **TilePalette resolves all 16 fields** — `VTP` reads `true`, `VTPVERIFY`
  reads `OK`, `all[]` has 16 entries.
- **GroundPalette keeps all 3 groups and ~54 tufts** (9 rows x 6 usable
  columns, less any absent), so `VGP` is around 66.
- **`VTPSKINN` is 5.** This is the prediction I am least confident of — B42 may
  not ship all five wall sheets. 3 or 4 would not surprise me and is not a bug.
- **Zero divergences.** If there is one, I expect it in TilePalette first,
  because it is the only unit whose result depends on the ENTIRE name set.

## If it diverges, read the VIN lines FIRST

    VIN  tiles    <count>  <hash>
    VIN  sprites  <count>  <hash>

These fingerprint the INPUTS. If the counts or hashes differ between the two
files, **the fault is in `TileIndex::load` or `PackFile`, not in the palette
units**, and no amount of reading tilepalette.cpp will find it. Only if the two
VIN lines match is a later divergence attributable to this chunk.

## Honest status of this code

The vanilla path has NOT been executed against a real install — this session has
no PZ install, so it is the one part of the chunk that is compiled-and-reviewed
rather than run. It is tested only to the extent that both sides refuse cleanly
on an empty media directory (Java throws, C++ exits 3 with a message) rather
than emitting an empty digest that would compare equal for the wrong reason.

Expect the first run to need a fix. That is not a reason to skip it.

## Numbers from this run are EXPIRING (CHARTER §4)

Tile and sprite counts change when the game updates. Whatever `VIN` reports goes
into FINDINGS stamped with the PZ build it was measured against, not recorded as
a permanent fact.
