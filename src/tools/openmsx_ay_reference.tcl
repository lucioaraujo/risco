# Risco AY/YM external reference harness.
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Lúcio de Araújo
# Requires RISCO_AY_CASE and RISCO_AY_OUTPUT in the environment.

if {![info exists ::env(RISCO_AY_CASE)] || ![info exists ::env(RISCO_AY_OUTPUT)]} {
  error "RISCO_AY_CASE and RISCO_AY_OUTPUT are required"
}

set throttle off

proc wr {reg value} {
  debug write "PSG regs" $reg $value
}

proc start_case {} {
for {set reg 0} {$reg <= 13} {incr reg} { wr $reg 0 }

switch -- $::env(RISCO_AY_CASE) {
  envelope_period_0 {
    wr 0 244; wr 1 1; wr 7 62; wr 8 16
    wr 11 1; wr 11 0; wr 12 0; wr 13 14
  }
  envelope_period_1 {
    wr 0 244; wr 1 1; wr 7 62; wr 8 16
    wr 11 1; wr 12 0; wr 13 14
  }
  envelope_shape_14 {
    wr 0 244; wr 1 1; wr 7 62; wr 8 16
    wr 11 128; wr 12 0; wr 13 14
  }
  envelope_retrigger {
    wr 0 244; wr 1 1; wr 7 62; wr 8 16
    wr 11 128; wr 12 0; wr 13 14
    after time 0.5 { wr 13 14 }
  }
  mixer_off {
    wr 0 244; wr 1 1; wr 6 8; wr 7 63; wr 8 15
  }
  mixer_tone {
    wr 0 244; wr 1 1; wr 6 8; wr 7 62; wr 8 15
  }
  mixer_noise {
    wr 0 244; wr 1 1; wr 6 8; wr 7 55; wr 8 15
  }
  mixer_both {
    wr 0 244; wr 1 1; wr 6 8; wr 7 54; wr 8 15
  }
  write_mid_counter {
    wr 0 188; wr 1 2; wr 7 62; wr 8 15
    after time 0.50001 { wr 0 250; wr 1 0 }
  }
  tone_period_0 {
    wr 0 1; wr 0 0; wr 1 0; wr 7 62; wr 8 15
  }
  tone_period_1 {
    wr 0 1; wr 1 0; wr 7 62; wr 8 15
  }
  tone_period_4095 {
    wr 0 255; wr 1 15; wr 7 62; wr 8 15
  }
  noise_period_0 {
    wr 6 1; wr 6 0; wr 7 55; wr 8 15
  }
  noise_period_1 {
    wr 6 1; wr 7 55; wr 8 15
  }
  noise_period_31 {
    wr 6 31; wr 7 55; wr 8 15
  }
  default {
    if {[regexp {^envelope_shape_([0-9]|1[0-5])$} $::env(RISCO_AY_CASE) _ shape]} {
      wr 0 244; wr 1 1; wr 7 62; wr 8 16
      wr 11 128; wr 12 0; wr 13 $shape
    } else {
      error "unknown RISCO_AY_CASE: $::env(RISCO_AY_CASE)"
    }
  }
}

soundlog start $::env(RISCO_AY_OUTPUT)
after time 1.0 {
  soundlog stop
  exit
}
}

# Let the emulated BIOS finish its PSG power-on writes first.
after time 0.1 start_case
