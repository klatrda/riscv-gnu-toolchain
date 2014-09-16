#=========================================================================
# crt0.S : Entry point for RISC-V user programs
#=========================================================================

  .text
  .global _start
_start:
  la      gp, _gp                 # Initialize global pointer

# clear the bss segment
  la      t0, _fbss
  la      t1, _end
1:
#ifdef __riscv64
  sd      zero,0(t0)
  addi    t0, t0, 8
#else
  sw      zero,0(t0)
  addi    t0, t0, 4
#endif
  bltu    t0, t1, 1b

  la      a0, __libc_fini_array   # Register global termination functions
  call    atexit                  #  to be called upon exit
  call    __libc_init_array       # Run global initialization functions

  lw      a0, 0(sp)               # a0 = argc
  addi    a1, sp, _RISCV_SZPTR/8  # a1 = argv
  li      a2, 0                   # a2 = envp = NULL
  call    main

  move    a0, v0                  # Call exit function with return value
  jump    exit                    #  from main as exit code

  .global _init
  .global _fini
_init:
_fini:
  # These don't have to do anything since we use init_array/fini_array.
  ret