;;-------------------------------------------------------------------------------------------------------
;; Copyright (C) Microsoft Corporation and contributors. All rights reserved.
;; Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
;;-------------------------------------------------------------------------------------------------------

(module
  (import "dummy" "memory" (memory 1))

    (func (export "func_i8x16_shuffle_test0")
        (local $v1 v128) (local $v2 v128)

        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))

        i32.const 0
        get_local $v1
        get_local $v2
        v8x16.shuffle  0x1c1d1e1f 0x13021101 0x06050403 0x0a0b1415 
        v128.store offset=0 align=4
    )

    (func (export "func_i8x16_shuffle_test1")
        (local $v1 v128) (local $v2 v128)

        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))

        i32.const 0
        get_local $v1
        get_local $v2
        v8x16.shuffle  0x13121110 0x17161514 0x03020100 0x07060504 
        v128.store align=4
    )

    (func (export "func_i8x16_shuffle_test2")
        (local $v1 v128) (local $v2 v128)

        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))

        i32.const 0
        get_local 0
        get_local 1
        v8x16.shuffle  0x12011100 0x14031302 0x16051504 0x18071706 
        v128.store align=4
    )

    (func (export "func_i32x4_bitselect") (local $v1 v128) (local $v2 v128) (local $mask v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (set_local $mask (v128.load offset=0 align=4 (i32.const 32)))
        (v128.store offset=0 align=4 (i32.const 0) (v128.bitselect (get_local $v1) (get_local $v2) (get_local $mask)))
    )

    (func (export "func_i32x4_add") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.add (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i32x4_sub") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.sub (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i32x4_mul") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.mul (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i32x4_shl") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.shl (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i32x4_shr_s") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.shr_s (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i32x4_shr_u") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i32x4.shr_u (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i16x8_add") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.add (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_addsaturate_s") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.add_saturate_s (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_addsaturate_u") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.add_saturate_u (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_sub") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.sub (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_subsaturate_s") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.sub_saturate_s (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_subsaturate_u") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.sub_saturate_u (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_mul") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.mul (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i16x8_shl") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.shl (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i16x8_shr_s") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.shr_s (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i16x8_shr_u") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i16x8.shr_u (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i8x16_add") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.add (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_addsaturate_s") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.add_saturate_s (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_addsaturate_u") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.add_saturate_u (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_sub") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.sub (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_subsaturate_s") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.sub_saturate_s (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_subsaturate_u") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.sub_saturate_u (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_mul") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.mul (get_local $v1) (get_local $v2)))
    )

    (func (export "func_i8x16_shl") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.shl (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i8x16_shr_s") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.shr_s (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_i8x16_shr_u") (param $shamt i32) (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (i8x16.shr_u (get_local $v1) (get_local $shamt)))
    )

    (func (export "func_f32x4_add") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.add (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_sub") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.sub (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_mul") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.mul (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_div") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.div (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_min") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.min (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_max") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.max (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f32x4_abs")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.abs (get_local $v1)))
    )

    (func (export "func_f32x4_sqrt")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f32x4.sqrt (get_local $v1)))
    )

    (func (export "func_f64x2_add") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.add (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_sub") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.sub (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_mul") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.mul (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_div") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.div (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_min") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.min (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_max") (local $v1 v128) (local $v2 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (set_local $v2 (v128.load offset=0 align=4 (i32.const 16)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.max (get_local $v1) (get_local $v2)))
    )

    (func (export "func_f64x2_abs")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.abs (get_local $v1)))
    )

    (func (export "func_f64x2_sqrt")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.sqrt (get_local $v1)))
    )

    (func (export "func_i64x2_trunc_s")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (i64x2.trunc_s/f64x2:sat (get_local $v1)))
    )

    (func (export "func_i64x2_trunc_u")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (i64x2.trunc_u/f64x2:sat (get_local $v1)))
    )

    (func (export "func_f64x2_convert_s")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.convert_s/i64x2 (get_local $v1)))
    )

    (func (export "func_f64x2_convert_u")  (local $v1 v128)
        (set_local $v1 (v128.load offset=0 align=4 (i32.const 0)))
        (v128.store offset=0 align=4 (i32.const 0) (f64x2.convert_u/i64x2 (get_local $v1)))
    )
)
