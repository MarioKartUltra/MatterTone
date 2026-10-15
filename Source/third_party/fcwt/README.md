fCWT (fast continuous wavelet transform), https://github.com/fastlib/fCWT
Copyright 2020-2021 Lukas Arts, Apache License 2.0 (LICENSE.txt).
Copied from upstream commit 49cb5a9ece028ced1365e372b5655ec6b55e08b7:
fcwt.h unchanged; fcwt.cpp with one added `#include <cassert>` (noted in
the file). Compiled scalar and single-threaded here (no AVX, no OpenMP).
