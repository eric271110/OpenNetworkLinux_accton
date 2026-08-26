# Accton x86-64 shared kernel modules

This directory holds kernel-module source that is shared across
multiple Accton x86-64 platforms. Files here are meant to be merged
into a platform's per-build staging tree by the platform's own
`modules/builds/Makefile` (see `common-modules.mk` under each platform).

Layout:

```
common/modules/
└── src/                    Shared kernel sources and headers
    ├── accton_ipmi_intf.c  IPMI helper module (built as accton_ipmi_intf.ko)
    └── accton_ipmi_intf.h  Public API for accton_ipmi_intf
```

## Adding a shared source to a platform

1. Add the file(s) here under `common/modules/src/`.
2. In the platform's `<variant>/modules/builds/Makefile`, list the
   shared basenames via `ACCTON_COMMON_KMOD_SRCS`, for example:

    ```make
    ACCTON_COMMON_KMOD_SRCS := accton_ipmi_intf
    include $(ONL)/packages/platforms/accton/x86-64/common/modules/kmodule-stage.mk
    ```

   `kmodule-stage.mk` copies the per-platform sources plus the listed
   shared basenames into a temporary staging directory, generates a
   merged `Makefile` (`obj-m += ...`), and hands the result to
   `kmodbuild.sh` via the standard `KMODULES` variable.

3. Remove the per-platform copies of the shared source files and drop
   the corresponding `obj-m` lines from the platform's
   `src/modules/Makefile`.
