# Verifying the C++ port

Self-tests prove internal consistency, which per Charter §4 proves very little
on its own: read and write can share a wrong assumption and agree perfectly.
Everything here is a comparison against an independent source — the Java tree,
and retail map data.

## Build

```fish
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 1. Cross-language oracle (no retail data needed)

`tools/Oracle.java` goes into `src/main/java/pzformat/` in the Java tree. It
shares no code with the C++ side; agreement is evidence.

```fish
# Java tree
javac -d out src/main/java/pzformat/LE.java src/main/java/pzformat/LEW.java \
             src/main/java/pzformat/LotHeader.java src/main/java/pzformat/LotPack.java \
             src/main/java/pzformat/RoundTrip.java src/main/java/pzformat/Oracle.java

# Java writes a B42 lotheader; C++ reads it, checks the decoded values, rewrites it
java -cp out pzformat.Oracle emit /tmp/h_java.bin
./build/pz_oracle check /tmp/h_java.bin /tmp/h_cpp.bin
cmp /tmp/h_java.bin /tmp/h_cpp.bin

# and back the other way
java -cp out pzformat.Oracle check /tmp/h_cpp.bin

# C++ writes a lotpack; the Java decoder and encoder reproduce it
./build/pz_oracle emitpack /tmp/pack_cpp.lotpack
java -cp out pzformat.Oracle pack /tmp/pack_cpp.lotpack
```

The synthetic header is deliberately awkward: a tile name with bytes `80 FF`,
an empty tile name, an empty room name, a negative `floor`, a negative
`minLevel`, a negative object field, and a zero-room building. Those are the
cases where a Latin-1/UTF-8 slip or a sign error would show up.

## 2. Harness output diff (needs retail data)

`pz_roundtrip` mirrors `RoundTrip.java` including its output text, so the two
can be run over the same directory and diffed line for line.

```fish
set MAP ~/.local/share/Steam/steamapps/common/ProjectZomboid/media/maps/Muldraugh,\ KY

java -cp out pzformat.Oracle sweep $MAP > /tmp/rt_java.txt
./build/pz_roundtrip $MAP           > /tmp/rt_cpp.txt
diff /tmp/rt_java.txt /tmp/rt_cpp.txt
```

**Predict before running** (Charter §4): both should report the same cell count,
100% byte-identical lotheaders, and `SPAN_LEVELS_MINIMAL` reproducing every
chunk. Any difference at all is a port bug with a cell name attached. A `diff`
that is empty is the result that matters; the numbers themselves are already
known from the Java side.

## 3. The check that hasn't been run yet

The minimal-levels encode policy computes its level count from the last square
that holds data, then writes every square up to that level's boundary. So a
chunk body that stopped immediately after its last square — with no trailing
run — could not round-trip; it would gain one. Since round-tripping succeeds
across the dataset, retail must always pad to the end of the last encoded level.

That is inferred from the round-trip result rather than measured. Direct
falsifier: for every chunk, `squaresCovered` should be an exact multiple of 64.
If any chunk is not, `SPAN_LEVELS_MINIMAL` is not the retail policy and it
scored 100% for a reason we do not understand.
