# Fuzzing xxUTF

To make sure that the xxUTF algorithms are robust, we support fuzzing using
[AFL++](https://github.com/AFLplusplus/AFLplusplus). Fuzzing is run in CI but
can also be run through a local machine using a Docker container.

Running the fuzzer locally via Docker requires only `docker` to be installed.
To get started, run the following at the root of the project:

```
docker build -t fuzz -f test/Dockerfile .
```

If you want to start a container in interactive mode:

```
docker run -v "$(pwd)/output:/output" -v "$(pwd):/code" -it fuzz
```

This will start a container. Other options can be added as needed. Two volumes
can be mounted:

- `/output`, which can be used to collect AFL++'s output data
- `/code`, which can be used to build xxUTF within the docker container

Once in the container, xxUTF should be built using the special AFL++
compiler, `afl-cc`:

```
afl-cc -o fuzz_target -O3 -Icode code/zig-out/amalgamation.c code/test/fuzz.c $(pkg-config --cflags --libs icu-uc icu-i18n)
```

This will create a `fuzz_target`
[instrumented binary](https://github.com/AFLplusplus/AFLplusplus/blob/stable/instrumentation/README.persistent_mode.md)
that `afl-fuzz` can feed inputs to. `icu4c` is linked during the compilation
of [`fuzz.c`](https://github.com/dzfrias/xxutf/blob/main/test/fuzz.c) because
`fuzz.c` will test each output of `xxutf` against `icu4c` and trigger a crash if
they do not match.

Finally, the command to run `afl-fuzz` on the instrumented binary:

```
afl-fuzz -i code/test/corpus -o /output -- ./fuzz_target
```

It is important to note that this will, by default, _only_ fuzz **NFD
normalization**. If you would like to test another xxUTF algorithm
or change the encoding, you may set the following environment variables:

```
XXUTF_FUZZ_ALGORITHM="NFC"
XXUTF_FUZZ_ENCODING="UTF-16LE"
```
