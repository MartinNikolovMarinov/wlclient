#pragma once

#if defined(_WIN32) || defined(_WIN64) || defined(__MINGW32__) || defined(__MINGW64__)
    #define OS_WIN 1
#else
    #define OS_WIN 0
#endif
#if defined(__linux__)
    #define OS_LINUX 1
#else
    #define OS_LINUX 0
#endif
#if defined(__APPLE__) || defined(__MACH__)
    #define OS_MAC 1
#else
    #define OS_MAC 0
#endif
#if OS_WIN == 0 && OS_LINUX == 0 && OS_MAC == 0
    #define OS_UNKNOWN 1
#else
    #define OS_UNKNOWN 0
#endif

// Most of these are unsupported, and will never be supported, but the detection code is good.
#if defined(__x86_64__) || defined(_M_X64)
    // Supported
    #define CPU_ARCH "x86_64"
    #define CPU_ARCH_X86_64 1
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
    #define CPU_ARCH "x86_32"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define CPU_ARCH "ARM64"
    #define CPU_ARCH_ARM64 1
#elif defined(__arm__)
    #define CPU_ARCH "ARM32"
#elif defined(mips) || defined(__mips__) || defined(__mips)
    #define CPU_ARCH "MIPS"
#elif defined(__sh__)
    #define CPU_ARCH "SUPERH"
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
    #define CPU_ARCH "POWERPC64"
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
    #define CPU_ARCH "POWERPC"
#elif defined(__sparc__) || defined(__sparc)
    #define CPU_ARCH "SPARC"
#elif defined(__m68k__)
    #define CPU_ARCH "M68K"
#elif defined(__riscv) || defined(__riscv__) || defined(RISCVEL)
    #if __riscv_xlen == 32
        #define CPU_ARCH "RISC-V32"
    #elif __riscv_xlen == 64
        #define CPU_ARCH "RISC-V64"
    #else
        #define CPU_ARCH "UNKNOWN RISC-V"
    #endif
#else
    #define CPU_ARCH "UNKNOWN"
#endif
