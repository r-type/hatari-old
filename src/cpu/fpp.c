/*
* UAE - The Un*x Amiga Emulator
*
* MC68881/68882/68040/68060 FPU emulation
*
* Copyright 1996 Herman ten Brugge
* Modified 2005 Peter Keunecke
* 68040+ exceptions and more by Toni Wilen
*/

#define __USE_ISOC9X  /* We might be able to pick up a NaN */

#include <math.h>
#include <float.h>
#include <fenv.h>

#include "main.h"
#include "hatari-glue.h"


#include "sysconfig.h"
#include "sysdeps.h"

#ifdef _MSC_VER
#pragma fenv_access(on)
#endif

#include "options_cpu.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "md-fpp.h"
#include "savestate.h"
#include "cpu_prefetch.h"
#include "cpummu.h"
#include "cpummu030.h"
//#include "debug.h"

#ifdef WITH_SOFTFLOAT
#include "softfloatx80.h"
#endif

#ifdef X86_MSVC_ASSEMBLY
#define X86_MSVC_ASSEMBLY_FPU
#define NATIVE_FPUCW
#endif

#define DEBUG_FPP 0
#define EXCEPTION_FPP 1

STATIC_INLINE int isinrom (void)
{
	return (munge24 (m68k_getpc ()) & 0xFFF80000) == 0xF80000 && !currprefs.mmu_model;
}

#ifdef WITH_SOFTFLOAT
static uae_u32 xhex_pi[]    ={0x2168c235, 0xc90fdaa2, 0x4000};
uae_u32 xhex_exp_1[] ={0xa2bb4a9a, 0xadf85458, 0x4000};
static uae_u32 xhex_l2_e[]  ={0x5c17f0bc, 0xb8aa3b29, 0x3fff};
static uae_u32 xhex_ln_2[]  ={0xd1cf79ac, 0xb17217f7, 0x3ffe};
uae_u32 xhex_ln_10[] ={0xaaa8ac17, 0x935d8ddd, 0x4000};
uae_u32 xhex_l10_2[] ={0xfbcff798, 0x9a209a84, 0x3ffd};
uae_u32 xhex_l10_e[] ={0x37287195, 0xde5bd8a9, 0x3ffd};
uae_u32 xhex_1e16[]  ={0x04000000, 0x8e1bc9bf, 0x4034};
uae_u32 xhex_1e32[]  ={0x2b70b59e, 0x9dc5ada8, 0x4069};
uae_u32 xhex_1e64[]  ={0xffcfa6d5, 0xc2781f49, 0x40d3};
uae_u32 xhex_1e128[] ={0x80e98ce0, 0x93ba47c9, 0x41a8};
uae_u32 xhex_1e256[] ={0x9df9de8e, 0xaa7eebfb, 0x4351};
uae_u32 xhex_1e512[] ={0xa60e91c7, 0xe319a0ae, 0x46a3};
uae_u32 xhex_1e1024[]={0x81750c17, 0xc9767586, 0x4d48};
uae_u32 xhex_1e2048[]={0xc53d5de5, 0x9e8b3b5d, 0x5a92};
uae_u32 xhex_1e4096[]={0x8a20979b, 0xc4605202, 0x7525};
static uae_u32 xhex_inf[]   ={0x00000000, 0x00000000, 0x7fff};
static uae_u32 xhex_nan[]   ={0xffffffff, 0xffffffff, 0x7fff};
static uae_u32 xhex_snan[]  ={0xffffffff, 0xbfffffff, 0x7fff};
#endif

#if USE_LONG_DOUBLE
static long double *fp_pi     = (long double *)xhex_pi;
static long double *fp_exp_1  = (long double *)xhex_exp_1;
static long double *fp_l2_e   = (long double *)xhex_l2_e;
static long double *fp_ln_2   = (long double *)xhex_ln_2;
static long double *fp_ln_10  = (long double *)xhex_ln_10;
static long double *fp_l10_2  = (long double *)xhex_l10_2;
static long double *fp_l10_e  = (long double *)xhex_l10_e;
static long double *fp_1e16   = (long double *)xhex_1e16;
static long double *fp_1e32   = (long double *)xhex_1e32;
static long double *fp_1e64   = (long double *)xhex_1e64;
static long double *fp_1e128  = (long double *)xhex_1e128;
static long double *fp_1e256  = (long double *)xhex_1e256;
static long double *fp_1e512  = (long double *)xhex_1e512;
static long double *fp_1e1024 = (long double *)xhex_1e1024;
static long double *fp_1e2048 = (long double *)xhex_1e2048;
static long double *fp_1e4096 = (long double *)xhex_1e4096;
//static long double *fp_inf    = (long double *)xhex_inf;
static long double *fp_nan    = (long double *)xhex_nan;
#else
static uae_u32 dhex_pi[]    ={0x54442D18, 0x400921FB};
static uae_u32 dhex_exp_1[] ={0x8B145769, 0x4005BF0A};
static uae_u32 dhex_l2_e[]  ={0x652B82FE, 0x3FF71547};
static uae_u32 dhex_ln_2[]  ={0xFEFA39EF, 0x3FE62E42};
static uae_u32 dhex_ln_10[] ={0xBBB55516, 0x40026BB1};
static uae_u32 dhex_l10_2[] ={0x509F79FF, 0x3FD34413};
static uae_u32 dhex_l10_e[] ={0x1526E50E, 0x3FDBCB7B};
static uae_u32 dhex_1e16[]  ={0x37E08000, 0x4341C379};
static uae_u32 dhex_1e32[]  ={0xB5056E17, 0x4693B8B5};
static uae_u32 dhex_1e64[]  ={0xE93FF9F5, 0x4D384F03};
static uae_u32 dhex_1e128[] ={0xF9301D32, 0x5A827748};
static uae_u32 dhex_1e256[] ={0x7F73BF3C, 0x75154FDD};
static uae_u32 dhex_inf[]   ={0x00000000, 0x7ff00000};
static uae_u32 dhex_nan[]   ={0xffffffff, 0x7fffffff};
static double *fp_pi     = (double *)dhex_pi;
static double *fp_exp_1  = (double *)dhex_exp_1;
static double *fp_l2_e   = (double *)dhex_l2_e;
static double *fp_ln_2   = (double *)dhex_ln_2;
static double *fp_ln_10  = (double *)dhex_ln_10;
static double *fp_l10_2  = (double *)dhex_l10_2;
static double *fp_l10_e  = (double *)dhex_l10_e;
static double *fp_1e16   = (double *)dhex_1e16;
static double *fp_1e32   = (double *)dhex_1e32;
static double *fp_1e64   = (double *)dhex_1e64;
static double *fp_1e128  = (double *)dhex_1e128;
static double *fp_1e256  = (double *)dhex_1e256;
static double *fp_1e512  = (double *)dhex_inf;
static double *fp_1e1024 = (double *)dhex_inf;
static double *fp_1e2048 = (double *)dhex_inf;
static double *fp_1e4096 = (double *)dhex_inf;
//static double *fp_inf    = (double *)dhex_inf;
static double *fp_nan    = (double *)dhex_nan;
#endif
double fp_1e8 = 1.0e8;
float  fp_1e0 = 1, fp_1e1 = 10, fp_1e2 = 100, fp_1e4 = 10000;
static bool fpu_mmu_fixup;

#define FFLAG_Z	    0x4000
#define FFLAG_N	    0x0100
#define FFLAG_NAN   0x0400


#ifdef WITH_SOFTFLOAT
static floatx80 fxsizes[6] = { 0 };
static floatx80 fxzero;
static floatx80 fx_1e0, fx_1e1, fx_1e2, fx_1e4, fx_1e8;
struct float_status_t fxstatus;
#endif
static fptype fsizes[] = { -128.0, 127.0, -32768.0, 32767.0, -2147483648.0, 2147483647.0 };

#define FP_INEXACT (1 << 9)
#define FP_DIVBYZERO (1 << 10)
#define FP_UNDERFLOW (1 << 11)
#define FP_OVERFLOW (1 << 12)
#define FP_OPERAND (1 << 13)
#define FP_SNAN (1 << 14)
#define FP_BSUN (1 << 15)

STATIC_INLINE void MAKE_FPSR (fptype *fp)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return;
#endif
	int status = fetestexcept (FE_ALL_EXCEPT);
	if (status) {
		if (status & FE_INEXACT)
			regs.fp_result_status |= FP_INEXACT;
		if (status & FE_DIVBYZERO)
			regs.fp_result_status |= FP_DIVBYZERO;
		if (status & FE_UNDERFLOW)
			regs.fp_result_status |= FP_UNDERFLOW;
		if (status & FE_OVERFLOW)
			regs.fp_result_status |= FP_OVERFLOW;
		if (status & FE_INVALID)
			regs.fp_result_status |= FP_OPERAND;
	}
	regs.fp_result.fp = *fp;
}

#ifdef WITH_SOFTFLOAT
STATIC_INLINE void MAKE_FPSR_SOFTFLOAT(floatx80 fx)
{
	if (fxstatus.float_exception_flags & float_flag_invalid)
		regs.fp_result_status |= FP_OPERAND;
	if (fxstatus.float_exception_flags & float_flag_divbyzero)
		regs.fp_result_status |= FP_DIVBYZERO;
	if (fxstatus.float_exception_flags & float_flag_overflow)
		regs.fp_result_status |= FP_OVERFLOW;
	if (fxstatus.float_exception_flags & float_flag_underflow)
		regs.fp_result_status |= FP_UNDERFLOW;
	if (fxstatus.float_exception_flags & float_flag_inexact)
		regs.fp_result_status |= FP_INEXACT;
	regs.fp_result.fpx = fx;
}
#endif

STATIC_INLINE void CLEAR_STATUS (void)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return;
#endif
	feclearexcept (FE_ALL_EXCEPT);
}

#ifdef WITH_SOFTFLOAT
static void softfloat_set(floatx80 *fx, uae_u32 *f)
{
	fx->exp = (uae_u16)f[2];
	fx->fraction = ((uae_u64)f[1] << 32) | f[0];
}
static void softfloat_get(floatx80 *fx, uae_u32 *f)
{
	f[2] = fx->exp;
	f[1] = fx->fraction >> 32;
	f[0] = (uae_u32)fx->fraction;
}
#endif

static void fpnan (fpdata *fpd)
{
	fpd->fp = *fp_nan;
#ifdef WITH_SOFTFLOAT
	softfloat_set(&fpd->fpx, xhex_nan);
#endif
}

static void fpclear (fpdata *fpd)
{
	fpd->fp = 0;
#ifdef WITH_SOFTFLOAT
	fpd->fpx = int32_to_floatx80(0);
#endif
}
static void fpset (fpdata *fpd, uae_s32 val)
{
	fpd->fp = (fptype)val;
#ifdef WITH_SOFTFLOAT
	fpd->fpx = int32_to_floatx80(val);
#endif
}

void to_single(fpdata *fpd, uae_u32 value)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float32 f = value;
		fpd->fpx = float32_to_floatx80(f, fxstatus);
	} else
#endif
		fpd->fp = to_single_x(value);
}
static uae_u32 from_single(fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float32 f = floatx80_to_float32(fpd->fpx, fxstatus);
		return f;
	} else
#endif
		return from_single_x(fpd->fp);
}
void to_double(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float64 f = ((float64)wrd1 << 32) | wrd2;
		fpd->fpx = float64_to_floatx80(f, fxstatus);
	} else
#endif
		fpd->fp = to_double_x(wrd1, wrd2);
}
static void from_double(fpdata *fpd, uae_u32 *wrd1, uae_u32 *wrd2)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float64 f = floatx80_to_float64(fpd->fpx, fxstatus);
		*wrd1 = f >> 32;
		*wrd2 = (uae_u32)f;
		return;
	} else
#endif
		return from_double_x(fpd->fp, wrd1, wrd2);
}
void to_exten(fpdata *fpd, uae_u32 wrd1, uae_u32 wrd2, uae_u32 wrd3)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		fpd->fpx.exp = wrd1 >> 16;
		fpd->fpx.fraction = ((uae_u64)wrd2 << 32) | wrd3;
#if 0
		if ((currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882) || currprefs.fpu_no_unimplemented) {
			// automatically fix denormals if 6888x or no implemented emulation
			Bit64u Sig = extractFloatx80Frac(fpd->fpx);
			Bit32s Exp = extractFloatx80Exp(fpd->fpx);
			if (Exp == 0 && Sig != 0)
				normalizeFloatx80Subnormal(Sig, &Exp, &Sig);
		}
#endif
	} else
#endif
		to_exten_x(&fpd->fp, wrd1, wrd2, wrd3);
}
static void from_exten(fpdata *fpd, uae_u32 * wrd1, uae_u32 * wrd2, uae_u32 * wrd3)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		*wrd1 = fpd->fpx.exp << 16;
		*wrd2 = fpd->fpx.fraction >> 32;
		*wrd3 = (uae_u32)fpd->fpx.fraction;
	} else
#endif
		from_exten_x(fpd->fp, wrd1, wrd2, wrd3);
}


#if 0
static void normalize(uae_u32 *pwrd1, uae_u32 *pwrd2, uae_u32 *pwrd3)
{
	uae_u32 wrd1 = *pwrd1;
	uae_u32 wrd2 = *pwrd2;
	uae_u32 wrd3 = *pwrd3;
	int exp = (wrd1 >> 16) & 0x7fff;
	// Normalize if unnormal.
	if (exp != 0 && exp != 0x7fff && !(wrd2 & 0x80000000)) {
		while (!(wrd2 & 0x80000000) && (wrd2 || wrd3)) {
			wrd2 <<= 1;
			if (wrd3 & 0x80000000)
				wrd2 |= 1;
			wrd3 <<= 1;
			exp--;
		}
		if (exp < 0)
			exp = 0;
		if (!wrd2 && !wrd3)
			exp = 0;
		*pwrd1 = (wrd1 & 0x80000000) | (exp << 16);
		*pwrd2 = wrd2;
		*pwrd3 = wrd3;
	}
}
#endif

static bool fpu_get_constant_fp(fpdata *fp, int cr)
{
	fptype f;
	switch (cr & 0x7f)
	{
		case 0x00:
		f = *fp_pi;
		break;
		case 0x0b:
		f = *fp_l10_2;
		break;
		case 0x0c:
		f = *fp_exp_1;
		break;
		case 0x0d:
		f = *fp_l2_e;
		break;
		case 0x0e:
		f = *fp_l10_e;
		break;
		case 0x0f:
		f = 0.0;
		break;
		case 0x30:
		f = *fp_ln_2;
		break;
		case 0x31:
		f = *fp_ln_10;
		break;
		case 0x32:
		f = (fptype)fp_1e0;
		break;
		case 0x33:
		f = (fptype)fp_1e1;
		break;
		case 0x34:
		f = (fptype)fp_1e2;
		break;
		case 0x35:
		f = (fptype)fp_1e4;
		break;
		case 0x36:
		f = (fptype)fp_1e8;
		break;
		case 0x37:
		f = *fp_1e16;
		break;
		case 0x38:
		f = *fp_1e32;
		break;
		case 0x39:
		f = *fp_1e64;
		break;
		case 0x3a:
		f = *fp_1e128;
		break;
		case 0x3b:
		f = *fp_1e256;
		break;
		case 0x3c:
		f = *fp_1e512;
		break;
		case 0x3d:
		f = *fp_1e1024;
		break;
		case 0x3e:
		f = *fp_1e2048;
		break;
		case 0x3f:
		f = *fp_1e4096;
		break;
		default:
		return false;
	}
	fp->fp = f;
	return true;
}

#ifdef WITH_SOFTFLOAT
static bool fpu_get_constant_softfloat(fpdata *fp, int cr)
{
	uae_u32 *f = NULL;
	floatx80 fx;

	switch (cr & 0x7f)
	{
		case 0x00:
		f = xhex_pi;
		break;
		case 0x0b:
		f = xhex_l10_2;
		break;
		case 0x0c:
		f = xhex_exp_1;
		break;
		case 0x0d:
		f = xhex_l2_e;
		break;
		case 0x0e:
		f = xhex_l10_e;
		break;
		case 0x0f:
		fx = fxzero;
		break;
		case 0x30:
		f = xhex_ln_2;
		break;
		case 0x31:
		f = xhex_ln_10;
		break;
		case 0x32:
		fx = fx_1e0;
		break;
		case 0x33:
		fx = fx_1e1;
		break;
		case 0x34:
		fx = fx_1e2;
		break;
		case 0x35:
		fx = fx_1e4;
		break;
		case 0x36:
		fx = fx_1e8;
		break;
		case 0x37:
		f = xhex_1e16;
		break;
		case 0x38:
		f = xhex_1e32;
		break;
		case 0x39:
		f = xhex_1e64;
		break;
		case 0x3a:
		f = xhex_1e128;
		break;
		case 0x3b:
		f = xhex_1e256;
		break;
		case 0x3c:
		f = xhex_1e512;
		break;
		case 0x3d:
		f = xhex_1e1024;
		break;
		case 0x3e:
		f = xhex_1e2048;
		break;
		case 0x3f:
		f = xhex_1e4096;
		break;
		default:
		return false;
	}
	if (f)
		softfloat_set(&fp->fpx, f);
	else
		fp->fpx = fx;
	return true;
}
#endif

bool fpu_get_constant(fpdata *fp, int cr)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return fpu_get_constant_softfloat(fp, cr);
#endif
	return fpu_get_constant_fp(fp, cr);
}

static void native_set_fpucw (uae_u32 m68k_cw)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		switch((m68k_cw >> 6) & 3)
		{
			case 0: // X
			default: // undefined
				fxstatus.float_rounding_precision = 80;
			break;
			case 1: // S
				fxstatus.float_rounding_precision = 32;
			break;
			case 2: // D
				fxstatus.float_rounding_precision = 64;
			break;
		}
		switch((m68k_cw >> 4) & 3)
		{
			case 0: // to neareset
				fxstatus.float_rounding_precision = float_round_nearest_even;
			break;
			case 1: // to zero
				fxstatus.float_rounding_mode = float_round_to_zero;
			break;
			case 2: // to minus
				fxstatus.float_rounding_mode = float_round_down;
			break;
			case 3: // to plus
				fxstatus.float_rounding_mode = float_round_up;
			break;
		}
	} else
#endif
	{
#ifdef NATIVE_FPUCW
#ifdef _WIN32
		static int ex = 0;
		// RN, RZ, RM, RP
		static const unsigned int fp87_round[4] = { _RC_NEAR, _RC_CHOP, _RC_DOWN, _RC_UP };
		// Extend X, Single S, Double D, Undefined
		static const unsigned int fp87_prec[4] = { _PC_64 , _PC_24 , _PC_53, 0 };
#ifdef WIN64
		_controlfp (ex | fp87_round[(m68k_cw >> 4) & 3], _MCW_RC);
#else
		_control87 (ex | fp87_round[(m68k_cw >> 4) & 3] | fp87_prec[(m68k_cw >> 6) & 3], _MCW_RC | _MCW_PC);
#endif
#else
	static const uae_u16 x87_cw_tab[] = {
		0x137f, 0x1f7f, 0x177f, 0x1b7f,	/* Extended */
		0x107f, 0x1c7f, 0x147f, 0x187f,	/* Single */
		0x127f, 0x1e7f, 0x167f, 0x1a7f,	/* Double */
		0x137f, 0x1f7f, 0x177f, 0x1b7f	/* undefined */
	};
#if USE_X86_FPUCW
		uae_u16 x87_cw = x87_cw_tab[(m68k_cw >> 4) & 0xf];

#if defined(X86_MSVC_ASSEMBLY)
		__asm {
			fldcw word ptr x87_cw
		}
#elif defined(X86_ASSEMBLY)
		__asm__ ("fldcw %0" : : "m" (*&x87_cw));
#endif
#endif
#endif
#endif
	}
}

#if defined(uae_s64) /* Close enough for government work? */
typedef uae_s64 tointtype;
#else
typedef uae_s32 tointtype;
#endif

/*
static void fpu_format_error (void)
{
	uaecptr newpc;
	regs.t0 = regs.t1 = 0;
	MakeSR ();
	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs.isp;
	}
	regs.s = 1;
	m68k_areg (regs, 7) -= 2;
	x_put_long (m68k_areg (regs, 7), 0x0000 + 14 * 4);
	m68k_areg (regs, 7) -= 4;
	x_put_long (m68k_areg (regs, 7), m68k_getpc ());
	m68k_areg (regs, 7) -= 2;
	x_put_long (m68k_areg (regs, 7), regs.sr);
	newpc = x_get_long (regs.vbr + 14 * 4);
	m68k_setpc (newpc);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	regs.fp_exception = true;
}
*/

#define FPU_EXP_UNIMP_INS 0
#define FPU_EXP_DISABLED 1
#define FPU_EXP_UNIMP_DATATYPE_PRE 2
#define FPU_EXP_UNIMP_DATATYPE_POST 3
#define FPU_EXP_UNIMP_DATATYPE_PACKED_PRE 4
#define FPU_EXP_UNIMP_DATATYPE_PACKED_POST 5
#define FPU_EXP_UNIMP_EA 6


static void fpu_op_unimp (uae_u16 opcode, uae_u16 extra, uae_u32 ea, uaecptr oldpc, int type, fpdata *src, int reg, int size)
{
	/* 68040 unimplemented/68060 FPU disabled exception.
	* Line F exception with different stack frame.. */
	int vector = 11;
	uaecptr newpc = m68k_getpc (); // next instruction
	static int warned = 20;

	regs.t0 = regs.t1 = 0;
	MakeSR ();
	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model == 68060) {
			m68k_areg (regs, 7) = regs.isp;
		} else if (currprefs.cpu_model >= 68020) {
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		} else {
			m68k_areg (regs, 7) = regs.isp;
		}
		regs.s = 1;
		if (currprefs.mmu_model)
			mmu_set_super (regs.s != 0);
	}
	regs.fpu_exp_state = 1;
	if (currprefs.cpu_model == 68060) {
		regs.fpiar = oldpc;
		regs.exp_extra = extra;
		regs.exp_opcode = opcode;
		regs.exp_size = size;
		if (src)
			regs.exp_src1 = *src;
		regs.exp_type = type;
		if (type == FPU_EXP_DISABLED) {
			// current PC
			newpc = oldpc;
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), oldpc);
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ea);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x4000 + vector * 4);
		} else if (type == FPU_EXP_UNIMP_INS) {
			// PC = next instruction
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ea);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
		} else if (type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST || type == FPU_EXP_UNIMP_DATATYPE_PRE || type == FPU_EXP_UNIMP_DATATYPE_POST) {
			regs.fpu_exp_state = 2; // EXC frame
			// PC = next instruction
			vector = 55;
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ea);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
		} else { // FPU_EXP_UNIMP_EA
			// current PC
			newpc = oldpc;
			vector = 60;
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x0000 + vector * 4);
		}
	} else if (currprefs.cpu_model == 68040) {
		regs.fpiar = oldpc;
		regs.exp_extra = extra;
		regs.exp_opcode = opcode;
		regs.exp_size = size;
		if (src)
			regs.exp_src1 = *src;
		regs.exp_type = type;
		if (reg >= 0)
			regs.exp_src2 = regs.fp[reg];
		else
			fpclear (&regs.exp_src2);
		if (type == FPU_EXP_UNIMP_INS || type == FPU_EXP_DISABLED) {
			// PC = next instruction
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ea);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
		} else if (type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST || type == FPU_EXP_UNIMP_DATATYPE_PRE || type == FPU_EXP_UNIMP_DATATYPE_POST) {
			// PC = next instruction
			vector = 55;
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ea);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x2000 + vector * 4);
			regs.fpu_exp_state = 2; // BUSY frame
		}
	}
	oldpc = newpc;
	m68k_areg (regs, 7) -= 4;
	x_put_long (m68k_areg (regs, 7), newpc);
	m68k_areg (regs, 7) -= 2;
	x_put_word (m68k_areg (regs, 7), regs.sr);
	newpc = x_get_long (regs.vbr + vector * 4);
	if (warned > 0) {
		write_log (_T("FPU EXCEPTION %d OP=%04X-%04X EA=%08X PC=%08X -> %08X\n"), type, opcode, extra, ea, oldpc, newpc);
#if EXCEPTION_FPP == 0
		warned--;
#endif
	}
	regs.fp_exception = true;
	m68k_setpc (newpc);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
}

static void fpu_op_illg2 (uae_u16 opcode, uae_u16 extra, uae_u32 ea, uaecptr oldpc)
{
	if ((currprefs.cpu_model == 68060 && (currprefs.fpu_model == 0 || (regs.pcr & 2)))
		|| (currprefs.cpu_model == 68040 && currprefs.fpu_model == 0)) {
			fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_DISABLED, NULL, -1, -1);
			return;
	}
	regs.fp_exception = true;
	m68k_setpc (oldpc);
	op_illg (opcode);
}

static void fpu_op_illg (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
	fpu_op_illg2 (opcode, extra, 0, oldpc);
}


static void fpu_noinst (uae_u16 opcode, uaecptr pc)
{
#if EXCEPTION_FPP
	write_log (_T("Unknown FPU instruction %04X %08X\n"), opcode, pc);
#endif
	regs.fp_exception = true;
	m68k_setpc (pc);
	op_illg (opcode);
}

static bool fault_if_no_fpu (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc)
{
	if ((regs.pcr & 2) || currprefs.fpu_model <= 0) {
#if EXCEPTION_FPP
		write_log (_T("no FPU: %04X-%04X PC=%08X\n"), opcode, extra, oldpc);
#endif
		fpu_op_illg2 (opcode, extra, ea, oldpc);
		return true;
	}
	return false;
}

static bool fault_if_unimplemented_680x0 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fpdata *src, int reg)
{
	if (fault_if_no_fpu (opcode, extra, ea, oldpc))
		return true;
	if (currprefs.cpu_model >= 68040 && currprefs.fpu_model && currprefs.fpu_no_unimplemented) {
		if ((extra & (0x8000 | 0x2000)) != 0)
			return false;
		if ((extra & 0xfc00) == 0x5c00) {
			// FMOVECR
			fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
			return true;
		}
		uae_u16 v = extra & 0x7f;
		switch (v)
		{
			case 0x01: /* FINT */
			case 0x03: /* FINTRZ */
			// Unimplemented only in 68040.
			if (currprefs.cpu_model == 68040) {
				fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
				return true;
			}
			return false;
			case 0x02: /* FSINH */
			case 0x06: /* FLOGNP1 */
			case 0x08: /* FETOXM1 */
			case 0x09: /* FTANH */
			case 0x0a: /* FATAN */
			case 0x0c: /* FASIN */
			case 0x0d: /* FATANH */
			case 0x0e: /* FSIN */
			case 0x0f: /* FTAN */
			case 0x10: /* FETOX */
			case 0x11: /* FTWOTOX */
			case 0x12: /* FTENTOX */
			case 0x14: /* FLOGN */
			case 0x15: /* FLOG10 */
			case 0x16: /* FLOG2 */
			case 0x19: /* FCOSH */
			case 0x1c: /* FACOS */
			case 0x1d: /* FCOS */
			case 0x1e: /* FGETEXP */
			case 0x1f: /* FGETMAN */
			case 0x30: /* FSINCOS */
			case 0x31: /* FSINCOS */
			case 0x32: /* FSINCOS */
			case 0x33: /* FSINCOS */
			case 0x34: /* FSINCOS */
			case 0x35: /* FSINCOS */
			case 0x36: /* FSINCOS */
			case 0x37: /* FSINCOS */
			case 0x21: /* FMOD */
			case 0x25: /* FREM */
			case 0x26: /* FSCALE */
			fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, src, reg, -1);
			return true;
		}
	}
	return false;
}

static bool fault_if_unimplemented_6888x (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
	if ((currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882) && currprefs.fpu_no_unimplemented) {
		uae_u16 v = extra & 0x7f;
		/* 68040/68060 only variants. 6888x = F-line exception. */
		switch (v)
		{
			case 0x62: /* FSADD */
			case 0x66: /* FDADD */
			case 0x68: /* FSSUB */
			case 0x6c: /* FDSUB */
			case 0x5a: /* FSNEG */
			case 0x5e: /* FDNEG */
			case 0x58: /* FSABS */
			case 0x5c: /* FDABS */
			case 0x63: /* FSMUL */
			case 0x67: /* FDMUL */
			case 0x41: /* FSSQRT */
			case 0x45: /* FDSQRT */
			fpu_noinst (opcode, oldpc);
			return true;
		}
	}
	return false;
}

static bool fault_if_60 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, int type)
{
	if (currprefs.cpu_model == 68060 && currprefs.fpu_model && currprefs.fpu_no_unimplemented) {
		fpu_op_unimp (opcode, extra, ea, oldpc, type, NULL, -1, -1);
		return true;
	}
	return false;
}

static bool fault_if_4060 (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, int type, fpdata *src, uae_u32 *pack)
{
	if (currprefs.cpu_model >= 68040 && currprefs.fpu_model && currprefs.fpu_no_unimplemented) {
		if (pack) {
			regs.exp_pack[0] = pack[0];
			regs.exp_pack[1] = pack[1];
			regs.exp_pack[2] = pack[2];
		}
		fpu_op_unimp (opcode, extra, ea, oldpc, type, src, -1, -1);
		return true;
	}
	return false;
}

static bool fault_if_no_fpu_u (uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc)
{
	if (fault_if_no_fpu (opcode, extra, ea, oldpc))
		return true;
	if (currprefs.cpu_model == 68060 && currprefs.fpu_model && currprefs.fpu_no_unimplemented) {
		// 68060 FTRAP, FDBcc or FScc are not implemented.
		fpu_op_unimp (opcode, extra, ea, oldpc, FPU_EXP_UNIMP_INS, NULL, -1, -1);
		return true;
	}
	return false;
}

static bool fault_if_no_6888x (uae_u16 opcode, uae_u16 extra, uaecptr oldpc)
{
	if (currprefs.cpu_model < 68040 && currprefs.fpu_model <= 0) {
#if EXCEPTION_FPP
		write_log (_T("6888x no FPU: %04X-%04X PC=%08X\n"), opcode, extra, oldpc);
#endif
		m68k_setpc (oldpc);
		regs.fp_exception = true;
		op_illg (opcode);
		return true;
	}
	return false;
}

static int get_fpu_version (void)
{
	int v = 0;

	switch (currprefs.fpu_model)
	{
	case 68881:
	case 68882:
		v = 0x1f;
		break;
	case 68040:
		if (currprefs.fpu_revision == 0x40)
			v = 0x40;
		else
			v = 0x41;
		break;
	}
	return v;
}

static void fpu_null (void)
{
	int i;
	regs.fpu_state = 0;
	regs.fpu_exp_state = 0;
	regs.fpcr = 0;
	regs.fpsr = 0;
	regs.fpiar = 0;
	fpclear (&regs.fp_result);
	for (i = 0; i < 8; i++)
		fpnan (&regs.fp[i]);
}

#define fp_round_to_minus_infinity(x) floor(x)
#define fp_round_to_plus_infinity(x) ceil(x)
#define fp_round_to_zero(x)	((x) >= 0.0 ? floor(x) : ceil(x))
#define fp_round_to_nearest(x) ((x) >= 0.0 ? (int)((x) + 0.5) : (int)((x) - 0.5))

static tointtype toint(fpdata *src, int size)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		if (floatx80_compare(src->fpx, fxsizes[size * 2 + 0], fxstatus) == float_relation_greater)
			return floatx80_to_int32(fxsizes[size * 2 + 0], fxstatus);	
		if (floatx80_compare(src->fpx, fxsizes[size * 2 + 1], fxstatus) == float_relation_less)
			return floatx80_to_int32(fxsizes[size * 2 + 1], fxstatus);
		return floatx80_to_int32(src->fpx, fxstatus);
	} else
#endif
	{
		fptype fp = src->fp;
		if (fp < fsizes[size * 2 + 0])
			fp = fsizes[size * 2 + 0];
		if (fp > fsizes[size * 2 + 1])
			fp = fsizes[size * 2 + 1];
	#if defined(X86_MSVC_ASSEMBLY_FPU)
		{
			fptype tmp_fp;
			__asm {
				fld  LDPTR fp
				frndint
				fstp LDPTR tmp_fp
			}
			return (tointtype)tmp_fp;
		}
	#else /* no X86_MSVC */
		{
			int result = (int)fp;
			switch (regs.fpcr & 0x30)
			{
				case FPCR_ROUND_ZERO:
					result = (int)fp_round_to_zero (fp);
					break;
				case FPCR_ROUND_MINF:
					result = (int)fp_round_to_minus_infinity (fp);
					break;
				case FPCR_ROUND_NEAR:
					result = fp_round_to_nearest (fp);
					break;
				case FPCR_ROUND_PINF:
					result = (int)fp_round_to_plus_infinity (fp);
					break;
			}
			return result;
		}
	#endif
	}
}

static bool fp_is_snan(fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return floatx80_is_signaling_nan(fpd->fpx) != 0;
#endif
	return false;
}
static bool fp_is_nan (fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return floatx80_is_nan(fpd->fpx) != 0;
#endif
#ifdef HAVE_ISNAN
	return isnan(fpd->fp) != 0;
#else
	return false;
#endif
}
static bool fp_is_infinity (fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float_class_t fc = floatx80_class(fpd->fpx);
		return fc == float_negative_inf || fc == float_positive_inf;
	}
#endif
#ifdef _MSC_VER
	return !_finite (fpd->fp);
#elif defined(HAVE_ISINF)
	return isinf (fpd->fp);
#else
	return false;
#endif
}
static bool fp_is_zero(fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return floatx80_compare_quiet(fpd->fpx, fxzero, fxstatus) == float_relation_equal;
#endif
	return fpd->fp == 0.0;
}
static bool fp_is_neg(fpdata *fpd)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat)
		return extractFloatx80Sign(fpd->fpx) != 0;
#endif
	return fpd->fp < 0.0;
}

uae_u32 fpp_get_fpsr (void)
{
	uae_u32 answer = regs.fpsr & 0x00ff00f8;

	// exception status byte
	answer |= regs.fp_result_status;
	if (fp_is_snan(&regs.fp_result))
		answer |= 1 << 14;

	// accrued exception byte
	if (answer & ((1 << 14)  | (1 << 13)))
		answer |= 0x80; // IOP = SNAN | OPERR
	if (answer & (1 << 12))
		answer |= 0x40; // OVFL = OVFL
	if (answer & ((1 << 11) | (1 << 9)))
		answer |= 0x20; // UNFL = UNFL | INEX2
	if (answer & (1 << 10))
		answer |= 0x10; // DZ = DZ
	if (answer & ((1 << 12) | (1 << 9) | (1 << 8)))
		answer |= 0x08; // INEX = INEX1 | INEX2 | OVFL 

	regs.fpsr = answer;

	// condition code byte
	if (fp_is_nan (&regs.fp_result)) {
		answer |= 1 << 24;
	} else {
		if (fp_is_zero(&regs.fp_result))
			answer |= 1 << 26;
		if (fp_is_infinity (&regs.fp_result))
			answer |= 1 << 25;
	}
	if (fp_is_neg(&regs.fp_result))
		answer |= 1 << 27;
	return answer;
}

static void update_fpsr (uae_u32 v)
{
	regs.fp_result_status = v;
	fpp_get_fpsr ();
}

STATIC_INLINE void set_fpsr (uae_u32 x)
{
	regs.fpsr = x;
	regs.fp_result_status = 0;

	if (x & 0x01000000)
		fpnan (&regs.fp_result);
	else if (x & 0x04000000)
		fpset (&regs.fp_result, 0);
	else if (x & 0x08000000)
		fpset (&regs.fp_result, -1);
	else
		fpset (&regs.fp_result, 1);
}

static uae_u32 get_ftag (uae_u32 w1, uae_u32 w2, uae_u32 w3, int size)
{
	int exp = (w1 >> 16) & 0x7fff;
	
	if (exp == 0) {
		if (!w2 && !w3)
			return 1; // ZERO
		if (size == 0 || size == 1)
			return 5; // Single/double DENORMAL
		return 4; // Extended DENORMAL or UNNORMAL
	} else if (exp == 0x7fff)  {
		int s = w2 >> 30;
		int z = (w2 & 0x3fffffff) == 0 && w3 == 0;
		if ((s == 0 && !z) || (s == 2 && !z))
			return 2; // INF
		return 3; // NAN
	} else {
		if (!(w2 & 0x80000000))
			return 4; // Extended UNNORMAL
		return 0; // NORMAL
	}
}

/* single   : S  8*E 23*F */
/* double   : S 11*E 52*F */
/* extended : S 15*E 64*F */
/* E = 0 & F = 0 -> 0 */
/* E = MAX & F = 0 -> Infin */
/* E = MAX & F # 0 -> NotANumber */
/* E = biased by 127 (single) ,1023 (double) ,16383 (extended) */

static void to_pack (fpdata *fpd, uae_u32 *wrd)
{
	fptype d;
	char *cp;
	char str[100];

	cp = str;
	if (wrd[0] & 0x80000000)
		*cp++ = '-';
	*cp++ = (wrd[0] & 0xf) + '0';
	*cp++ = '.';
	*cp++ = ((wrd[1] >> 28) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 16) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 12) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 8) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 4) & 0xf) + '0';
	*cp++ = ((wrd[1] >> 0) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 28) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 16) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 12) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 8) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 4) & 0xf) + '0';
	*cp++ = ((wrd[2] >> 0) & 0xf) + '0';
	*cp++ = 'E';
	if (wrd[0] & 0x40000000)
		*cp++ = '-';
	*cp++ = ((wrd[0] >> 24) & 0xf) + '0';
	*cp++ = ((wrd[0] >> 20) & 0xf) + '0';
	*cp++ = ((wrd[0] >> 16) & 0xf) + '0';
	*cp = 0;
#if USE_LONG_DOUBLE
	sscanf (str, "%Le", &d);
#else
	sscanf (str, "%le", &d);
#endif
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		uae_u32 wrd[3];
		from_exten_x(d, &wrd[0], &wrd[1], &wrd[2]);
		softfloat_set(&fpd->fpx, wrd);
	} else
#endif
		fpd->fp = d;
}

static void from_pack (fpdata *src, uae_u32 *wrd, int kfactor)
{
	int i, j, t;
	int exp;
	int ndigits;
	char *cp, *strp;
	char str[100];
	fptype fp;

	wrd[0] = wrd[1] = wrd[2] = 0;

	if (fp_is_nan (src) || fp_is_infinity (src)) {
		wrd[0] |= (1 << 30) | (1 << 29) | (1 << 30); // YY=1
		wrd[0] |= 0xfff << 16; // Exponent=FFF
		// TODO: mantissa should be set if NAN
		return;
	}

#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		uae_u32 out[3];
		softfloat_get(&src->fpx, out);
		to_exten_x(&fp, out[0], out[1], out[2]);
	} else
#endif
		fp = src->fp;

#if USE_LONG_DOUBLE
	sprintf (str, "%#.17Le", fp);
#else
	sprintf (str, "%#.17e", fp);
#endif
	
	// get exponent
	cp = str;
	while (*cp++ != 'e');
	if (*cp == '+')
		cp++;
	exp = atoi (cp);

	// remove trailing zeros
	cp = str;
	while (*cp != 'e')
		cp++;
	cp[0] = 0;
	cp--;
	while (cp > str && *cp == '0') {
		*cp = 0;
		cp--;
	}

	cp = str;
	// get sign
	if (*cp == '-') {
		cp++;
		wrd[0] = 0x80000000;
	} else if (*cp == '+') {
		cp++;
	}
	strp = cp;

	if (kfactor <= 0) {
		ndigits = abs (exp) + (-kfactor) + 1;
	} else {
		if (kfactor > 17) {
			kfactor = 17;
			update_fpsr (FE_INVALID);
		}
		ndigits = kfactor;
	}

	if (ndigits < 0)
		ndigits = 0;
	if (ndigits > 16)
		ndigits = 16;

	// remove decimal point
	strp[1] = strp[0];
	strp++;
	// add trailing zeros
	i = strlen (strp);
	cp = strp + i;
	while (i < ndigits) {
		*cp++ = '0';
		i++;
	}
	i = ndigits + 1;
	while (i < 17) {
		strp[i] = 0;
		i++;
	}
	*cp = 0;
	i = ndigits - 1;
	// need to round?
	if (i >= 0 && strp[i + 1] >= '5') {
		while (i >= 0) {
			strp[i]++;
			if (strp[i] <= '9')
				break;
			if (i == 0) {
				strp[i] = '1';
				exp++;
			} else {
				strp[i] = '0';
			}
			i--;
		}
	}
	strp[ndigits] = 0;

	// store first digit of mantissa
	cp = strp;
	wrd[0] |= *cp++ - '0';

	// store rest of mantissa
	for (j = 1; j < 3; j++) {
		for (i = 0; i < 8; i++) {
			wrd[j] <<= 4;
			if (*cp >= '0' && *cp <= '9')
				wrd[j] |= *cp++ - '0';
		}
	}

	// exponent
	if (exp < 0) {
		wrd[0] |= 0x40000000;
		exp = -exp;
	}
	if (exp > 9999) // ??
		exp = 9999;
	if (exp > 999) {
		int d = exp / 1000;
		wrd[0] |= d << 12;
		exp -= d * 1000;
		update_fpsr (FE_INVALID);
	}
	i = 100;
	t = 0;
	while (i >= 1) {
		int d = exp / i;
		t <<= 4;
		t |= d;
		exp -= d * i;
		i /= 10;
	}
	wrd[0] |= t << 16;
}

// 68040/060 does not support denormals
static bool fault_if_no_denormal_support_pre(uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fpdata *fpd, int size)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.cpu_model >= 68040 && currprefs.fpu_model && currprefs.fpu_no_unimplemented && currprefs.fpu_softfloat) {
		Bit64u Sig = extractFloatx80Frac(fpd->fpx);
		Bit32s Exp = extractFloatx80Exp(fpd->fpx);
		if (Exp == 0 && Sig != 0) {
			fpu_op_unimp(opcode, extra, ea, oldpc, FPU_EXP_UNIMP_DATATYPE_PRE, fpd, -1, size);
			return true;
		}
	}
#endif
	return false;
}
static bool fault_if_no_denormal_support_post(uae_u16 opcode, uae_u16 extra, uaecptr ea, uaecptr oldpc, fpdata *fpd, int size)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat && currprefs.cpu_model >= 68040 && currprefs.fpu_model && currprefs.fpu_no_unimplemented) {
		Bit64u Sig = extractFloatx80Frac(fpd->fpx);
		Bit32s Exp = extractFloatx80Exp(fpd->fpx);
		if (Exp == 0 && Sig != 0) {
			fpu_op_unimp(opcode, extra, ea, oldpc, FPU_EXP_UNIMP_DATATYPE_POST, fpd, -1, size);
			return true;
		}
	}
#endif
	return false;
}

static int get_fp_value (uae_u32 opcode, uae_u16 extra, fpdata *src, uaecptr oldpc, uae_u32 *adp)
{
	int size, mode, reg;
	uae_u32 ad = 0;
	static const int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
	static const int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };
	uae_u32 exts[3];
	int doext = 0;

	if (!(extra & 0x4000)) {
		if (fault_if_no_fpu (opcode, extra, 0, oldpc))
			return -1;
		*src = regs.fp[(extra >> 10) & 7];
		if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 2))
			return -1;
		return 1;
	}
	mode = (opcode >> 3) & 7;
	reg = opcode & 7;
	size = (extra >> 10) & 7;

	switch (mode) {
		case 0:
			switch (size)
			{
				case 6:
					fpset(src, (uae_s8) m68k_dreg (regs, reg));
					break;
				case 4:
					fpset(src, (uae_s16) m68k_dreg (regs, reg));
					break;
				case 0:
					fpset(src, (uae_s32) m68k_dreg (regs, reg));
					break;
				case 1:
					to_single (src, m68k_dreg (regs, reg));
					if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 0))
						return -1;
					break;
				default:
					return 0;
			}
			return 1;
		case 1:
			return 0;
		case 2:
			ad = m68k_areg (regs, reg);
			break;
		case 3:
			if (currprefs.mmu_model) {
				mmufixup[0].reg = reg;
				mmufixup[0].value = m68k_areg (regs, reg);
				fpu_mmu_fixup = true;
			}
			ad = m68k_areg (regs, reg);
			m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
			break;
		case 4:
			if (currprefs.mmu_model) {
				mmufixup[0].reg = reg;
				mmufixup[0].value = m68k_areg (regs, reg);
				fpu_mmu_fixup = true;
			}
			m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
			ad = m68k_areg (regs, reg);
			break;
		case 5:
			ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
			break;
		case 6:
			ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
			break;
		case 7:
			switch (reg)
			{
				case 0: // (xxx).W
					ad = (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 1: // (xxx).L
					ad = x_cp_next_ilong ();
					break;
				case 2: // (d16,PC)
					ad = m68k_getpc ();
					ad += (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 3: // (d8,PC,Xn)+
					ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
					break;
				case 4: // #imm
					doext = 1;
					switch (size)
					{
						case 0: // L
						case 1: // S
						exts[0] = x_cp_next_ilong ();
						break;
						case 2: // X
						case 3: // P
						// 68060 and immediate X or P: unimplemented effective address
						if (fault_if_60 (opcode, extra, ad, oldpc, FPU_EXP_UNIMP_EA))
							return -1;
						exts[0] = x_cp_next_ilong ();
						exts[1] = x_cp_next_ilong ();
						exts[2] = x_cp_next_ilong ();
						break;
						case 4: // W
						exts[0] = x_cp_next_iword ();
						break;
						case 5: // D
						exts[0] = x_cp_next_ilong ();
						exts[1] = x_cp_next_ilong ();
						break;
						case 6: // B
						exts[0] = x_cp_next_iword ();
						break;
					}
					break;
				default:
					return 0;
			}
	}

	*adp = ad;

	if (currprefs.fpu_model == 68060 && fault_if_unimplemented_680x0 (opcode, extra, ad, oldpc, src, -1))
		return -1;

	switch (size)
	{
		case 0:
			fpset(src, (uae_s32) (doext ? exts[0] : x_cp_get_long (ad)));
			break;
		case 1:
			to_single (src, (doext ? exts[0] : x_cp_get_long (ad)));
			if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 0))
				return -1;
			break;
		case 2:
			{
				uae_u32 wrd1, wrd2, wrd3;
				wrd1 = (doext ? exts[0] : x_cp_get_long (ad));
				ad += 4;
				wrd2 = (doext ? exts[1] : x_cp_get_long (ad));
				ad += 4;
				wrd3 = (doext ? exts[2] : x_cp_get_long (ad));
				to_exten (src, wrd1, wrd2, wrd3);
				if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 2))
					return -1;
			}
			break;
		case 3:
			{
				uae_u32 wrd[3];
				uae_u32 adold = ad;
				if (currprefs.cpu_model == 68060) {
					if (fault_if_4060 (opcode, extra, adold, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_PRE, NULL, wrd))
						return -1;
				}
				wrd[0] = (doext ? exts[0] : x_cp_get_long (ad));
				ad += 4;
				wrd[1] = (doext ? exts[1] : x_cp_get_long (ad));
				ad += 4;
				wrd[2] = (doext ? exts[2] : x_cp_get_long (ad));
				if (fault_if_4060 (opcode, extra, adold, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_PRE, NULL, wrd))
					return -1;
				to_pack (src, wrd);
				return 1;
			}
			break;
		case 4:
			fpset(src, (uae_s16) (doext ? exts[0] : x_cp_get_word (ad)));
			break;
		case 5:
			{
				uae_u32 wrd1, wrd2;
				wrd1 = (doext ? exts[0] : x_cp_get_long (ad));
				ad += 4;
				wrd2 = (doext ? exts[1] : x_cp_get_long (ad));
				to_double (src, wrd1, wrd2);
				if (fault_if_no_denormal_support_pre(opcode, extra, 0, oldpc, src, 1))
					return -1;
			}
			break;
		case 6:
			fpset(src, (uae_s8) (doext ? exts[0] : x_cp_get_byte (ad)));
			break;
		default:
			return 0;
	}
	return 1;
}

static int put_fp_value (fpdata *value, uae_u32 opcode, uae_u16 extra, uaecptr oldpc)
{
	int size, mode, reg;
	uae_u32 ad = 0;
	static int sz1[8] = { 4, 4, 12, 12, 2, 8, 1, 0 };
	static int sz2[8] = { 4, 4, 12, 12, 2, 8, 2, 0 };

#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("PUTFP: %f %04X %04X\n"), value, opcode, extra);
#endif
	if (!(extra & 0x4000)) {
		if (fault_if_no_fpu (opcode, extra, 0, oldpc))
			return 1;
		regs.fp[(extra >> 10) & 7] = *value;
		return 1;
	}
	reg = opcode & 7;
	mode = (opcode >> 3) & 7;
	size = (extra >> 10) & 7;
	ad = -1;
	switch (mode)
	{
		case 0:
			switch (size)
			{
				case 6:
					m68k_dreg (regs, reg) = (uae_u32)(((toint (value, 0) & 0xff)
						| (m68k_dreg (regs, reg) & ~0xff)));
					break;
				case 4:
					m68k_dreg (regs, reg) = (uae_u32)(((toint (value, 1) & 0xffff)
						| (m68k_dreg (regs, reg) & ~0xffff)));
					break;
				case 0:
					m68k_dreg (regs, reg) = (uae_u32)toint (value, 2);
					break;
				case 1:
					m68k_dreg (regs, reg) = from_single (value);
					break;
				default:
					return 0;
			}
			return 1;
		case 1:
			return 0;
		case 2:
			ad = m68k_areg (regs, reg);
			break;
		case 3:
			if (currprefs.mmu_model) {
				mmufixup[0].reg = reg;
				mmufixup[0].value = m68k_areg (regs, reg);
				fpu_mmu_fixup = true;
			}
			ad = m68k_areg (regs, reg);
			m68k_areg (regs, reg) += reg == 7 ? sz2[size] : sz1[size];
			break;
		case 4:
			if (currprefs.mmu_model) {
				mmufixup[0].reg = reg;
				mmufixup[0].value = m68k_areg (regs, reg);
				fpu_mmu_fixup = true;
			}
			m68k_areg (regs, reg) -= reg == 7 ? sz2[size] : sz1[size];
			ad = m68k_areg (regs, reg);
			break;
		case 5:
			ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
			break;
		case 6:
			ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
			break;
		case 7:
			switch (reg)
			{
				case 0:
					ad = (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 1:
					ad = x_cp_next_ilong ();
					break;
				case 2:
					ad = m68k_getpc ();
					ad += (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 3:
					ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
					break;
				default:
					return 0;
			}
	}

	if (fault_if_no_fpu (opcode, extra, ad, oldpc))
		return 1;

	switch (size)
	{
		case 0:
			if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
				return 1;
			x_cp_put_long(ad, (uae_u32)toint(value, 2));
			break;
		case 1:
			if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
				return -1;
			x_cp_put_long(ad, from_single(value));
			break;
		case 2:
			{
				uae_u32 wrd1, wrd2, wrd3;
				if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
					return 1;
				from_exten(value, &wrd1, &wrd2, &wrd3);
				x_cp_put_long (ad, wrd1);
				ad += 4;
				x_cp_put_long (ad, wrd2);
				ad += 4;
				x_cp_put_long (ad, wrd3);
			}
			break;
		case 3: // Packed-Decimal Real with Static k-Factor
		case 7: // Packed-Decimal Real with Dynamic k-Factor (P{Dn}) (reg to memory only)
			{
				uae_u32 wrd[3];
				int kfactor;
				if (fault_if_4060 (opcode, extra, ad, oldpc, FPU_EXP_UNIMP_DATATYPE_PACKED_POST, value, NULL))
					return 1;
				kfactor = size == 7 ? m68k_dreg (regs, (extra >> 4) & 7) : extra;
				kfactor &= 127;
				if (kfactor & 64)
					kfactor |= ~63;
				from_pack (value, wrd, kfactor);
				x_cp_put_long (ad, wrd[0]);
				ad += 4;
				x_cp_put_long (ad, wrd[1]);
				ad += 4;
				x_cp_put_long (ad, wrd[2]);
			}
			break;
		case 4:
			if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
				return 1;
			x_cp_put_word(ad, (uae_s16)toint(value, 1));
			break;
		case 5:
			{
				uae_u32 wrd1, wrd2;
				if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 1))
					return -1;
				from_double(value, &wrd1, &wrd2);
				x_cp_put_long (ad, wrd1);
				ad += 4;
				x_cp_put_long (ad, wrd2);
			}
			break;
		case 6:
			if (fault_if_no_denormal_support_post(opcode, extra, ad, oldpc, value, 2))
				return 1;
			x_cp_put_byte(ad, (uae_s8)toint(value, 0));
			break;
		default:
			return 0;
	}
	return 1;
}

STATIC_INLINE int get_fp_ad (uae_u32 opcode, uae_u32 * ad)
{
	int mode;
	int reg;

	mode = (opcode >> 3) & 7;
	reg = opcode & 7;
	switch (mode)
	{
		case 0:
		case 1:
			return 0;
		case 2:
			*ad = m68k_areg (regs, reg);
			break;
		case 3:
			*ad = m68k_areg (regs, reg);
			break;
		case 4:
			*ad = m68k_areg (regs, reg);
			break;
		case 5:
			*ad = m68k_areg (regs, reg) + (uae_s32) (uae_s16) x_cp_next_iword ();
			break;
		case 6:
			*ad = x_cp_get_disp_ea_020 (m68k_areg (regs, reg), 0);
			break;
		case 7:
			switch (reg)
			{
				case 0:
					*ad = (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 1:
					*ad = x_cp_next_ilong ();
					break;
				case 2:
					*ad = m68k_getpc ();
					*ad += (uae_s32) (uae_s16) x_cp_next_iword ();
					break;
				case 3:
					*ad = x_cp_get_disp_ea_020 (m68k_getpc (), 0);
					break;
				default:
					return 0;
			}
	}
	return 1;
}

int fpp_cond (int condition)
{
	int NotANumber, Z, N;

	NotANumber = fp_is_nan(&regs.fp_result);
	N = fp_is_neg(&regs.fp_result);
	Z = fp_is_zero(&regs.fp_result);

	if ((condition & 0x10) && NotANumber)
		regs.fp_result_status |= FP_BSUN;

	switch (condition)
	{
		case 0x00:
			return 0;
		case 0x01:
			return Z;
		case 0x02:
			return !(NotANumber || Z || N);
		case 0x03:
			return Z || !(NotANumber || N);
		case 0x04:
			return N && !(NotANumber || Z);
		case 0x05:
			return Z || (N && !NotANumber);
		case 0x06:
			return !(NotANumber || Z);
		case 0x07:
			return !NotANumber;
		case 0x08:
			return NotANumber;
		case 0x09:
			return NotANumber || Z;
		case 0x0a:
			return NotANumber || !(N || Z);
		case 0x0b:
			return NotANumber || Z || !N;
		case 0x0c:
			return NotANumber || (N && !Z);
		case 0x0d:
			return NotANumber || Z || N;
		case 0x0e:
			return !Z;
		case 0x0f:
			return 1;
		case 0x10:
			return 0;
		case 0x11:
			return Z;
		case 0x12:
			return !(NotANumber || Z || N);
		case 0x13:
			return Z || !(NotANumber || N);
		case 0x14:
			return N && !(NotANumber || Z);
		case 0x15:
			return Z || (N && !NotANumber);
		case 0x16:
			return !(NotANumber || Z);
		case 0x17:
			return !NotANumber;
		case 0x18:
			return NotANumber;
		case 0x19:
			return NotANumber || Z;
		case 0x1a:
			return NotANumber || !(N || Z);
		case 0x1b:
			return NotANumber || Z || !N;
		case 0x1c:
			return NotANumber || (N && !Z);
		case 0x1d:
			return NotANumber || Z || N;
		case 0x1e:
			return !Z;
		case 0x1f:
			return 1;
	}
	return -1;
}

static void maybe_idle_state (void)
{
	// conditional floating point instruction does not change state
	// from null to idle on 68040/060.
	if (currprefs.fpu_model == 68881 || currprefs.fpu_model == 68882)
		regs.fpu_state = 1;
}

void fpuop_dbcc (uae_u32 opcode, uae_u16 extra)
{
	uaecptr pc = m68k_getpc ();
	uae_s32 disp;
	int cc;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("fdbcc_opp at %08x\n"), m68k_getpc ());
#endif
	if (fault_if_no_6888x (opcode, extra, pc - 4))
		return;

	disp = (uae_s32) (uae_s16) x_cp_next_iword ();
	if (fault_if_no_fpu_u (opcode, extra, pc + disp, pc - 4))
		return;
	regs.fpiar = pc - 4;
	maybe_idle_state ();
	cc = fpp_cond (extra & 0x3f);
	if (cc < 0) {
		fpu_op_illg (opcode, extra, regs.fpiar);
	} else if (!cc) {
		int reg = opcode & 0x7;

		m68k_dreg (regs, reg) = ((m68k_dreg (regs, reg) & 0xffff0000)
			| (((m68k_dreg (regs, reg) & 0xffff) - 1) & 0xffff));
		if ((m68k_dreg (regs, reg) & 0xffff) != 0xffff) {
			m68k_setpc (pc + disp);
			regs.fp_branch = true;
		}
	}
}

void fpuop_scc (uae_u32 opcode, uae_u16 extra)
{
	uae_u32 ad = 0;
	int cc;
	uaecptr pc = m68k_getpc () - 4;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("fscc_opp at %08x\n"), m68k_getpc ());
#endif

	if (fault_if_no_6888x (opcode, extra, pc))
		return;

	if (opcode & 0x38) {
		if (get_fp_ad (opcode, &ad) == 0) {
			fpu_noinst (opcode, regs.fpiar);
			return;
		}
	}

	if (fault_if_no_fpu_u (opcode, extra, ad, pc))
		return;

	regs.fpiar = pc;
	maybe_idle_state ();
	cc = fpp_cond (extra & 0x3f);
	if (cc < 0) {
		fpu_op_illg (opcode, extra, regs.fpiar);
	} else if ((opcode & 0x38) == 0) {
		m68k_dreg (regs, opcode & 7) = (m68k_dreg (regs, opcode & 7) & ~0xff) | (cc ? 0xff : 0x00);
	} else {
		x_cp_put_byte (ad, cc ? 0xff : 0x00);
	}
}

void fpuop_trapcc (uae_u32 opcode, uaecptr oldpc, uae_u16 extra)
{
	int cc;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("ftrapcc_opp at %08x\n"), m68k_getpc ());
#endif
	if (fault_if_no_fpu_u (opcode, extra, 0, oldpc))
		return;

	regs.fpiar = oldpc;
	maybe_idle_state ();
	cc = fpp_cond (extra & 0x3f);
	if (cc < 0) {
		fpu_op_illg (opcode, extra, oldpc);
	} else if (cc) {
		Exception (7);
	}
}

void fpuop_bcc (uae_u32 opcode, uaecptr oldpc, uae_u32 extra)
{
	int cc;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("fbcc_opp at %08x\n"), m68k_getpc ());
#endif
	if (fault_if_no_fpu (opcode, extra, 0, oldpc - 2))
		return;

	regs.fpiar = oldpc - 2;
	maybe_idle_state ();
	cc = fpp_cond (opcode & 0x3f);
	if (cc < 0) {
		fpu_op_illg (opcode, extra, oldpc - 2);
	} else if (cc) {
		if ((opcode & 0x40) == 0)
			extra = (uae_s32) (uae_s16) extra;
		m68k_setpc (oldpc + extra);
		regs.fp_branch = true;
	}
}

void fpuop_save (uae_u32 opcode)
{
	uae_u32 ad;
	int incr = (opcode & 0x38) == 0x20 ? -1 : 1;
	int fpu_version = get_fpu_version ();
	uaecptr pc = m68k_getpc () - 2;
	int i;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("fsave_opp at %08x\n"), m68k_getpc ());
#endif

	if (fault_if_no_6888x (opcode, 0, pc))
		return;

	if (get_fp_ad (opcode, &ad) == 0) {
		fpu_op_illg (opcode, 0, pc);
		return;
	}

	if (fault_if_no_fpu (opcode, 0, ad, pc))
		return;

	if (currprefs.fpu_model == 68060) {
		/* 12 byte 68060 NULL/IDLE/EXCP frame.  */
		int frame_size = 12;
		uae_u32 frame_id, frame_v1, frame_v2;
		
		if (regs.fpu_exp_state > 1) {
			uae_u32 src1[3];
			from_exten (&regs.exp_src1, &src1[0], &src1[1], &src1[2]);
			frame_id = 0x0000e000 | src1[0];
			frame_v1 = src1[1];
			frame_v2 = src1[2];

#if EXCEPTION_FPP
#if USE_LONG_DOUBLE
			write_log(_T("68060 FSAVE EXCP %Le\n"), regs.exp_src1.fp);
#else
			write_log(_T("68060 FSAVE EXCP %e\n"), regs.exp_src1.fp);
#endif
#endif

		} else {
			frame_id = regs.fpu_state == 0 ? 0x00000000 : 0x00006000;
			frame_v1 = 0;
			frame_v2 = 0;
		}
		if (incr < 0)
			ad -= frame_size;
		x_put_long (ad, frame_id);
		ad += 4;
		x_put_long (ad, frame_v1);
		ad += 4;
		x_put_long (ad, frame_v2);
		ad += 4;
		if (incr < 0)
			ad -= frame_size;
	} else if (currprefs.fpu_model == 68040) {
		if (!regs.fpu_exp_state) {
			/* 4 byte 68040 NULL/IDLE frame.  */
			uae_u32 frame_id = regs.fpu_state == 0 ? 0 : fpu_version << 24;
			if (incr < 0) {
				ad -= 4;
				x_put_long (ad, frame_id);
			} else {
				x_put_long (ad, frame_id);
				ad += 4;
			}
		} else {
			/* 44 (rev $40) and 52 (rev $41) byte 68040 unimplemented instruction frame */
			/* 96 byte 68040 busy frame */
			int frame_size = regs.fpu_exp_state == 2 ? 0x64 : (fpu_version >= 0x41 ? 0x34 : 0x2c);
			uae_u32 frame_id = ((fpu_version << 8) | (frame_size - 4)) << 16;
			uae_u32 src1[3], src2[3];
			uae_u32 stag, dtag;
			uae_u32 extra = regs.exp_extra;

			from_exten(&regs.exp_src1, &src1[0], &src1[1], &src1[2]);
			from_exten(&regs.exp_src2, &src2[0], &src2[1], &src2[2]);
			stag = get_ftag(src1[0], src1[1], src1[2], regs.exp_size);
			dtag = get_ftag(src2[0], src2[1], src2[2], -1);
			if ((extra & 0x7f) == 4) // FSQRT 4->5
				extra |= 1;

#if EXCEPTION_FPP
			write_log(_T("68040 FSAVE %d (%d), CMDREG=%04X"), regs.exp_type, frame_size, extra);
			if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE) {
				write_log(_T(" PACKED %08x-%08x-%08x"), regs.exp_pack[0], regs.exp_pack[1], regs.exp_pack[2]);
			} else if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST) {
#if USE_LONG_DOUBLE
				write_log(_T(" SRC=%Le (%08x-%08x-%08x %d), DST=%Le (%08x-%08x-%08x %d)"), regs.exp_src1.fp, src1[0], src1[1], src1[2], stag, regs.exp_src2.fp, src2[0], src2[1], src2[2], dtag);
#else
				write_log(_T(" SRC=%e (%08x-%08x-%08x %d), DST=%e (%08x-%08x-%08x %d)"), regs.exp_src1.fp, src1[0], src1[1], src1[2], stag, regs.exp_src2.fp, src2[0], src2[1], src2[2], dtag);
#endif
			}
			write_log(_T("\n"));
#endif

			if (incr < 0)
				ad -= frame_size;
			x_put_long (ad, frame_id);
			ad += 4;
			if (regs.fpu_exp_state == 2) {
				/* BUSY frame */
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, 0); // CU_SAVEPC (Software shouldn't care)
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, 0); // WBTS/WBTE (No E3 emulated yet)
				ad += 4;
				x_put_long(ad, 0); // WBTM
				ad += 4;
				x_put_long(ad, 0); // WBTM
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, regs.fpiar); // FPIARCU (same as FPU PC or something else?)
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
				x_put_long(ad, 0);
				ad += 4;
			}
			if (fpu_version >= 0x41 || regs.fpu_exp_state == 2) {
				x_put_long (ad, ((extra & (0x200 | 0x100 | 0x80)) | (extra & (0x40 | 0x02 | 0x01)) | ((extra >> 1) & (0x04 | 0x08 | 0x10)) | ((extra & 0x04) ? 0x20 : 0x00)) << 16); // CMDREG3B
				ad += 4;
				x_put_long (ad, 0);
				ad += 4;
			}
			x_put_long (ad, stag << 29); // STAG
			ad += 4;
			x_put_long (ad, extra << 16); // CMDREG1B
			ad += 4;
			x_put_long (ad, dtag << 29); // DTAG
			ad += 4;
			if (fpu_version >= 0x41 || regs.fpu_exp_state == 2) {
				x_put_long(ad, (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE ? 1 << 26 : 0) | (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST ? 1 << 20 : 0)); // E1 and T
				ad += 4;
			} else {
				x_put_long(ad, (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE || regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_POST) ? 1 << 26 : 0); // E1
				ad += 4;
			}
			if (regs.exp_type == FPU_EXP_UNIMP_DATATYPE_PACKED_PRE) {
				x_put_long (ad, 0); // FPTS/FPTE
				ad += 4;
				x_put_long (ad, 0); // FPTM
				ad += 4;
				x_put_long (ad, regs.exp_pack[0]); // FPTM
				ad += 4;
				x_put_long (ad, 0); // ETS/ETE
				ad += 4;
				x_put_long (ad, regs.exp_pack[1]); // ETM
				ad += 4;
				x_put_long (ad, regs.exp_pack[2]); // ETM
				ad += 4;
			} else {
				x_put_long (ad, src2[0]); // FPTS/FPTE
				ad += 4;
				x_put_long (ad, src2[1]); // FPTM
				ad += 4;
				x_put_long (ad, src2[2]); // FPTM
				ad += 4;
				x_put_long (ad, src1[0]); // ETS/ETE
				ad += 4;
				x_put_long (ad, src1[1]); // ETM
				ad += 4;
				x_put_long (ad, src1[2]); // ETM
				ad += 4;
			}
			if (incr < 0)
				ad -= frame_size;
		}
	} else { /* 68881/68882 */
		int frame_size_real = currprefs.fpu_model == 68882 ? 0x3c : 0x1c;;
		int frame_size = regs.fpu_state == 0 ? 0 : frame_size_real;
		uae_u32 frame_id = regs.fpu_state == 0 ? ((frame_size_real - 4) << 16) : (fpu_version << 24) | ((frame_size_real - 4) << 16);
	
		if (currprefs.mmu_model) {
			if (incr < 0) {
				for (i = 0; i < (frame_size / 4) - 1; i++) {
					ad -= 4;
					if (mmu030_state[0] == i) {
						x_put_long (ad, i == 0 ? 0x70000000 : 0x00000000);
						mmu030_state[0]++;
					}
				}
				ad -= 4;
				if (mmu030_state[0] == (frame_size / 4) - 1 || (mmu030_state[0] == 0 && frame_size == 0)) {
					x_put_long (ad, frame_id);
					mmu030_state[0]++;
				}
			} else {
				if (mmu030_state[0] == 0) {
					x_put_long (ad, frame_id);
					mmu030_state[0]++;
				}
				ad += 4;
				for (i = 0; i < (frame_size / 4) - 1; i++) {
					if (mmu030_state[0] == i + 1) {
						x_put_long (ad, i == (frame_size / 4) - 2 ? 0x70000000 : 0x00000000);
						mmu030_state[0]++;
					}
					ad += 4;
				}
			}
		} else {
			if (incr < 0) {
				for (i = 0; i < (frame_size / 4) - 1; i++) {
					ad -= 4;
					x_put_long (ad, i == 0 ? 0x70000000 : 0x00000000);
				}
				ad -= 4;
				x_put_long (ad, frame_id);
			} else {
				x_put_long (ad, frame_id);
				ad += 4;
				for (i = 0; i < (frame_size / 4) - 1; i++) {
					x_put_long (ad, i == (frame_size / 4) - 2 ? 0x70000000 : 0x00000000);
					ad += 4;
				}
			}
		}
	}

	if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad;
	regs.fpu_exp_state = 0;
}

void fpuop_restore (uae_u32 opcode)
{
#ifndef WINUAE_FOR_HATARI
	int fpu_version = get_fpu_version (); // TODO: check version of stack frame
#endif
	uaecptr pc = m68k_getpc () - 2;
	uae_u32 ad;
	uae_u32 d;
	int incr = (opcode & 0x38) == 0x20 ? -1 : 1;

	regs.fp_exception = false;
#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("frestore_opp at %08x\n"), m68k_getpc ());
#endif

	if (fault_if_no_6888x (opcode, 0, pc))
		return;

	if (get_fp_ad (opcode, &ad) == 0) {
		fpu_op_illg (opcode, 0, pc);
		return;
	}

	if (fault_if_no_fpu (opcode, 0, ad, pc))
		return;
	regs.fpiar = pc;

	if (incr < 0) {
		ad -= 4;
		d = x_get_long (ad);
	} else {
		d = x_get_long (ad);
		ad += 4;
	}

	if (currprefs.fpu_model == 68060) {
		int ff = (d >> 8) & 0xff;
		uae_u32 v1, v2;

		if (incr < 0) {
			ad -= 4;
			v1 = x_get_long (ad);
			ad -= 4;
			v2 = x_get_long (ad);
		} else {
			v1 = x_get_long (ad);
			ad += 4;
			v2 = x_get_long (ad);
			ad += 4;
		}
		if (ff == 0x60) {
			regs.fpu_state = 1;
			regs.fpu_exp_state = 0;
		} else if (ff == 0xe0) {
			regs.fpu_exp_state = 1;
			to_exten (&regs.exp_src1, d & 0xffff0000, v1, v2);
		} else if (ff) {
			write_log (_T("FRESTORE invalid frame format %X!\n"), (d >> 8) & 0xff);
		} else {
			fpu_null ();
		}
	} else {
		if ((d & 0xff000000) != 0) {
			regs.fpu_state = 1;
			if (incr < 0)
				ad -= (d >> 16) & 0xff;
			else
				ad += (d >> 16) & 0xff;
		} else {
			fpu_null ();
		}
	}

	if ((opcode & 0x38) == 0x18)
		m68k_areg (regs, opcode & 7) = ad;
	if ((opcode & 0x38) == 0x20)
		m68k_areg (regs, opcode & 7) = ad;
}

static uaecptr fmovem2mem (uaecptr ad, uae_u32 list, int incr, int regdir)
{
	int reg;

	// 68030 MMU state saving is annoying!
	if (currprefs.mmu_model == 68030) {
		int idx = 0;
		int r;
		int i;
		uae_u32 wrd[3];
		mmu030_state[1] |= MMU030_STATEFLAG1_MOVEM1;
		for (r = 0; r < 8; r++) {
			if (regdir < 0)
				reg = 7 - r;
			else
				reg = r;
			if (list & 0x80) {
				from_exten(&regs.fp[reg], &wrd[0], &wrd[1], &wrd[2]);
				if (incr < 0)
					ad -= 3 * 4;
				for (i = 0; i < 3; i++) {
					if (mmu030_state[0] == idx * 3 + i) {
						if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2) {
							mmu030_state[1] &= ~MMU030_STATEFLAG1_MOVEM2;
						}
						else {
							mmu030_data_buffer = wrd[i];
							x_put_long(ad + i * 4, wrd[i]);
						}
						mmu030_state[0]++;
					}
				}
				if (incr > 0)
					ad += 3 * 4;
				idx++;
			}
			list <<= 1;
		}
	} else {
		int r;
		for (r = 0; r < 8; r++) {
			uae_u32 wrd1, wrd2, wrd3;
			if (regdir < 0)
				reg = 7 - r;
			else
				reg = r;
			if (list & 0x80) {
				from_exten(&regs.fp[reg], &wrd1, &wrd2, &wrd3);
				if (incr < 0)
					ad -= 3 * 4;
				x_put_long(ad + 0, wrd1);
				x_put_long(ad + 4, wrd2);
				x_put_long(ad + 8, wrd3);
				if (incr > 0)
					ad += 3 * 4;
			}
			list <<= 1;
		}
	}
	return ad;
}

static uaecptr fmovem2fpp (uaecptr ad, uae_u32 list, int incr, int regdir)
{
	int reg;

	if (currprefs.mmu_model == 68030) {
		uae_u32 wrd[3];
		int idx = 0;
		int r;
		int i;
		mmu030_state[1] |= MMU030_STATEFLAG1_MOVEM1 | MMU030_STATEFLAG1_FMOVEM;
		if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2)
			ad = mmu030_ad[mmu030_idx].val;
		else
			mmu030_ad[mmu030_idx].val = ad;
		for (r = 0; r < 8; r++) {
			if (regdir < 0)
				reg = 7 - r;
			else
				reg = r;
			if (list & 0x80) {
				if (incr < 0)
					ad -= 3 * 4;
				for (i = 0; i < 3; i++) {
					if (mmu030_state[0] == idx * 3 + i) {
						if (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM2) {
							mmu030_state[1] &= ~MMU030_STATEFLAG1_MOVEM2;
							wrd[i] = mmu030_data_buffer;
						} else {
							wrd[i] = x_get_long (ad + i * 4);
						}
						// save first two entries if 2nd or 3rd get_long() faults.
						if (i == 0 || i == 1)
							mmu030_fmovem_store[i] = wrd[i];
						mmu030_state[0]++;
						if (i == 2)
							to_exten (&regs.fp[reg], mmu030_fmovem_store[0], mmu030_fmovem_store[1], wrd[2]);
					}
				}
				if (incr > 0)
					ad += 3 * 4;
				idx++;
			}
			list <<= 1;
		}
	} else {
		int r;
		for (r = 0; r < 8; r++) {
			uae_u32 wrd1, wrd2, wrd3;
			if (regdir < 0)
				reg = 7 - r;
			else
				reg = r;
			if (list & 0x80) {
				if (incr < 0)
					ad -= 3 * 4;
				wrd1 = x_get_long (ad + 0);
				wrd2 = x_get_long (ad + 4);
				wrd3 = x_get_long (ad + 8);
				if (incr > 0)
					ad += 3 * 4;
				to_exten (&regs.fp[reg], wrd1, wrd2, wrd3);
			}
			list <<= 1;
		}
	}
	return ad;
}

// round to float
static void fround (int reg)
{
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		float32 f = floatx80_to_float32(regs.fp[reg].fpx, fxstatus);
		regs.fp[reg].fpx = float32_to_floatx80(f, fxstatus);
	} else
#endif
		regs.fp[reg].fp = (float)regs.fp[reg].fp;
}

static bool arithmetic_fp(fptype src, int reg, int extra)
{
	bool sgl = false;
	switch (extra & 0x7f)
	{
		case 0x00: /* FMOVE */
		case 0x40: /* Explicit rounding. This is just a quick fix. */
		case 0x44: /* Same for all other cases that have three choices */
			regs.fp[reg].fp = src;        /* Brian King was here. */
			/*<ea> to register needs FPSR updated. See Motorola 68K Manual. */
			break;
		case 0x01: /* FINT */
			/* need to take the current rounding mode into account */
#if defined(X86_MSVC_ASSEMBLY_FPU)
			{
				fptype tmp_fp;
				__asm {
					fld  LDPTR src
					frndint
					fstp LDPTR tmp_fp
				}
				regs.fp[reg].fp = tmp_fp;
			}
#else /* no X86_MSVC */
			switch (regs.fpcr & 0x30)
			{
				case FPCR_ROUND_NEAR:
					regs.fp[reg].fp = fp_round_to_nearest(src);
					break;
				case FPCR_ROUND_ZERO:
					regs.fp[reg].fp = fp_round_to_zero(src);
					break;
				case FPCR_ROUND_MINF:
					regs.fp[reg].fp = fp_round_to_minus_infinity(src);
					break;
				case FPCR_ROUND_PINF:
					regs.fp[reg].fp = fp_round_to_plus_infinity(src);
					break;
				default: /* never reached */
					regs.fp[reg].fp = src;
					break;
			}
#endif /* X86_MSVC */
			break;
		case 0x02: /* FSINH */
			regs.fp[reg].fp = sinh (src);
			break;
		case 0x03: /* FINTRZ */
			regs.fp[reg].fp = fp_round_to_zero (src);
			break;
		case 0x04: /* FSQRT */
		case 0x41: /* FSSQRT */
		case 0x45: /* FDSQRT */
			regs.fp[reg].fp = sqrt (src);
			break;
		case 0x06: /* FLOGNP1 */
			regs.fp[reg].fp = log (src + 1.0);
			break;
		case 0x08: /* FETOXM1 */
			regs.fp[reg].fp = exp (src) - 1.0;
			break;
		case 0x09: /* FTANH */
			regs.fp[reg].fp = tanh (src);
			break;
		case 0x0a: /* FATAN */
			regs.fp[reg].fp = atan (src);
			break;
		case 0x0c: /* FASIN */
			regs.fp[reg].fp = asin (src);
			break;
		case 0x0d: /* FATANH */
			regs.fp[reg].fp = atanh (src);
			break;
		case 0x0e: /* FSIN */
			regs.fp[reg].fp = sin (src);
			break;
		case 0x0f: /* FTAN */
			regs.fp[reg].fp = tan (src);
			break;
		case 0x10: /* FETOX */
			regs.fp[reg].fp = exp (src);
			break;
		case 0x11: /* FTWOTOX */
			regs.fp[reg].fp = pow (2.0, src);
			break;
		case 0x12: /* FTENTOX */
			regs.fp[reg].fp = pow (10.0, src);
			break;
		case 0x14: /* FLOGN */
			regs.fp[reg].fp = log (src);
			break;
		case 0x15: /* FLOG10 */
			regs.fp[reg].fp = log10 (src);
			break;
		case 0x16: /* FLOG2 */
			regs.fp[reg].fp = *fp_l2_e * log (src);
			break;
		case 0x18: /* FABS */
		case 0x58: /* FSABS */
		case 0x5c: /* FDABS */
			regs.fp[reg].fp = src < 0 ? -src : src;
			break;
		case 0x19: /* FCOSH */
			regs.fp[reg].fp = cosh (src);
			break;
		case 0x1a: /* FNEG */
		case 0x5a: /* FSNEG */
		case 0x5e: /* FDNEG */
			regs.fp[reg].fp = -src;
			break;
		case 0x1c: /* FACOS */
			regs.fp[reg].fp = acos (src);
			break;
		case 0x1d: /* FCOS */
			regs.fp[reg].fp = cos (src);
			break;
		case 0x1e: /* FGETEXP */
			{
				if (src == 0) {
					regs.fp[reg].fp = 0;
				} else {
					int expon;
					frexp (src, &expon);
					regs.fp[reg].fp = (double) (expon - 1);
				}
			}
			break;
		case 0x1f: /* FGETMAN */
			{
				if (src == 0) {
					regs.fp[reg].fp = 0;
				} else {
					int expon;
					regs.fp[reg].fp = frexp (src, &expon) * 2.0;
				}
			}
			break;
		case 0x20: /* FDIV */
		case 0x60: /* FSDIV */
		case 0x64: /* FDDIV */
			regs.fp[reg].fp /= src;
			break;
		case 0x21: /* FMOD */
			{
				fptype quot = fp_round_to_zero(regs.fp[reg].fp / src);
				regs.fp[reg].fp = regs.fp[reg].fp - quot * src;
			}
			break;
		case 0x22: /* FADD */
		case 0x62: /* FSADD */
		case 0x66: /* FDADD */
			regs.fp[reg].fp += src;
			break;
		case 0x23: /* FMUL */
		case 0x63: /* FSMUL */
		case 0x67: /* FDMUL */
			regs.fp[reg].fp *= src;
			break;
		case 0x24: /* FSGLDIV */
			regs.fp[reg].fp /= src;
			sgl = true;
			break;
		case 0x25: /* FREM */
			{
				fptype quot = fp_round_to_nearest(regs.fp[reg].fp / src);
				regs.fp[reg].fp = regs.fp[reg].fp - quot * src;
			}
			break;
		case 0x26: /* FSCALE */
			if (src != 0) {
#ifdef ldexp
				regs.fp[reg] = ldexp (regs.fp[reg], (int) src);
#else
				regs.fp[reg].fp *= exp (*fp_ln_2 * (int) src);
#endif
			}
			break;
		case 0x27: /* FSGLMUL */
			regs.fp[reg].fp *= src;
			sgl = true;
			break;
		case 0x28: /* FSUB */
		case 0x68: /* FSSUB */
		case 0x6c: /* FDSUB */
			regs.fp[reg].fp -= src;
			break;
		case 0x30: /* FSINCOS */
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
			regs.fp[extra & 7].fp = cos (src);
			regs.fp[reg].fp = sin (src);
			break;
		case 0x38: /* FCMP */
			{
				fptype tmp = regs.fp[reg].fp - src;
				regs.fpsr = 0;
				MAKE_FPSR (&tmp);
			}
			return true;
		case 0x3a: /* FTST */
			regs.fpsr = 0;
			MAKE_FPSR (&src);
			return true;
		default:
			return false;
	}
	// round to float?
	if (sgl || (extra & 0x44) == 0x40)
		fround (reg);
	MAKE_FPSR (&regs.fp[reg].fp);
	return true;
}

#ifdef WITH_SOFTFLOAT
static bool arithmetic_softfloat(floatx80 *srcd, int reg, int extra)
{
	floatx80 fx = *srcd;
	floatx80 f = regs.fp[reg].fpx;
	int float_rounding_mode;
	bool sgl = false;
	Bit64u q;

	// SNAN -> QNAN if SNAN interrupt is not enabled
	if (floatx80_is_signaling_nan(fx) && !(regs.fpcr & 0x4000)) {
		fx.fraction |= 0x40000000;
	}

	switch (extra & 0x7f)
	{
		case 0x00: /* FMOVE */
		case 0x40:
		case 0x44:
			regs.fp[reg].fpx = fx;	
			break;
		case 0x01: /* FINT */
			regs.fp[reg].fpx = floatx80_round_to_int(fx, fxstatus);
			break;
		case 0x03: /* FINTRZ */
			float_rounding_mode = fxstatus.float_rounding_mode;
			fxstatus.float_rounding_mode = float_round_to_zero;
			regs.fp[reg].fpx = floatx80_round_to_int(fx, fxstatus);
			float_rounding_mode = fxstatus.float_rounding_mode;
			break;
		case 0x04: /* FSQRT */
		case 0x41: /* FSSQRT */
		case 0x45: /* FDSQRT */
			regs.fp[reg].fpx = floatx80_sqrt(fx, fxstatus);
			break;
		case 0x18: /* FABS */
		case 0x58: /* FSABS */
		case 0x5c: /* FDABS */
			regs.fp[reg].fpx = floatx80_abs(fx);
			break;
		case 0x1a: /* FNEG */
		case 0x5a: /* FSNEG */
		case 0x5e: /* FDNEG */
			// same here..
			regs.fp[reg].fpx = floatx80_chs(fx);
			break;
		case 0x20: /* FDIV */
		case 0x60: /* FSDIV */
		case 0x64: /* FDDIV */
			regs.fp[reg].fpx = floatx80_div(f, fx, fxstatus);
			break;
		case 0x22: /* FADD */
		case 0x62: /* FSADD */
		case 0x66: /* FDADD */
			regs.fp[reg].fpx = floatx80_add(f, fx, fxstatus);
			break;
		case 0x23: /* FMUL */
		case 0x63: /* FSMUL */
		case 0x67: /* FDMUL */
			regs.fp[reg].fpx = floatx80_mul(f, fx, fxstatus);
			break;
		case 0x24: /* FSGLDIV */
			regs.fp[reg].fpx = floatx80_div(f, fx, fxstatus);
			sgl = true;
			break;
		case 0x25: /* FREM */
			floatx80_ieee754_remainder(f, fx, regs.fp[reg].fpx, q, fxstatus);
			break;
		case 0x27: /* FSGLMUL */
			regs.fp[reg].fpx = floatx80_mul(f, fx, fxstatus);
			sgl = true;
			break;
		case 0x28: /* FSUB */
		case 0x68: /* FSSUB */
		case 0x6c: /* FDSUB */
			regs.fp[reg].fpx = floatx80_sub(f, fx, fxstatus);
			break;
		case 0x38: /* FCMP */
			f = floatx80_sub(f, fx, fxstatus);
			regs.fpsr = 0;
			MAKE_FPSR_SOFTFLOAT(f);
			return true;
		case 0x3a: /* FTST */
			regs.fpsr = 0;
			MAKE_FPSR_SOFTFLOAT(f);
			return true;

		case 0x1d: /* FCOS */
			fcos(f, fxstatus);
			regs.fp[reg].fpx = f;
			break;
		case 0x0e: /* FSIN */
			fsin(f, fxstatus);
			regs.fp[reg].fpx = f;
			break;
		case 0x0f: /* FTAN */
			ftan(f, fxstatus);
			regs.fp[reg].fpx = f;
			break;
		case 0x30: /* FSINCOS */
		case 0x31: /* FSINCOS */
		case 0x32: /* FSINCOS */
		case 0x33: /* FSINCOS */
		case 0x34: /* FSINCOS */
		case 0x35: /* FSINCOS */
		case 0x36: /* FSINCOS */
		case 0x37: /* FSINCOS */
			fsincos(f, &regs.fp[extra & 7].fpx, &regs.fp[reg].fpx, fxstatus);
			break;

			// some of following are supported by softfloat, later..
		case 0x06: /* FLOGNP1 */
		case 0x08: /* FETOXM1 */
		case 0x09: /* FTANH */
		case 0x0a: /* FATAN */
		case 0x0c: /* FASIN */
		case 0x0d: /* FATANH */
		case 0x10: /* FETOX */
		case 0x11: /* FTWOTOX */
		case 0x12: /* FTENTOX */
		case 0x14: /* FLOGN */
		case 0x15: /* FLOG10 */
		case 0x16: /* FLOG2 */
		case 0x19: /* FCOSH */
		case 0x1c: /* FACOS */
		case 0x1e: /* FGETEXP */
		case 0x1f: /* FGETMAN */
			{
				// This is horribly ineffective..
				fptype fp;
				uae_u32 out[3];
				// convert softfloat to raw words
				softfloat_get(&fx, out);
				// convert to double/long double
				to_exten_x(&fp, out[0], out[1], out[2]);
				// emulate instruction using normal fpu code
				if (!arithmetic_fp(fp, reg, extra))
					return false;
				// convert back to raw
				from_exten_x(regs.fp[reg].fp, &out[0], &out[1], &out[2]);
				// convert to softfloat internal format
				softfloat_set(&regs.fp[reg].fpx, out);
				MAKE_FPSR_SOFTFLOAT(regs.fp[reg].fpx);
			}
			break;
	}
	return true;
}
#endif

static void fpuop_arithmetic2 (uae_u32 opcode, uae_u16 extra)
{
	int reg = -1;
	int v;
	fpdata srcd;
	uaecptr pc = m68k_getpc () - 4;
	uaecptr ad = 0;

#if DEBUG_FPP
	if (!isinrom ())
		write_log (_T("FPP %04x %04x at %08x\n"), opcode & 0xffff, extra, pc);
#endif
	if (fault_if_no_6888x (opcode, extra, pc))
		return;

	switch ((extra >> 13) & 0x7)
	{
		case 3:
			if (put_fp_value (&regs.fp[(extra >> 7) & 7], opcode, extra, pc) == 0)
				fpu_noinst (opcode, pc);
			return;

		case 4:
		case 5:
			if ((opcode & 0x38) == 0) {
				if (fault_if_no_fpu (opcode, extra, 0, pc))
					return;
				if (extra & 0x2000) {
					if (extra & 0x1000)
						m68k_dreg (regs, opcode & 7) = regs.fpcr & 0xffff;
					if (extra & 0x0800)
						m68k_dreg (regs, opcode & 7) = fpp_get_fpsr ();
					if (extra & 0x0400)
						m68k_dreg (regs, opcode & 7) = regs.fpiar;
				} else {
					if (extra & 0x1000) {
						regs.fpcr = m68k_dreg (regs, opcode & 7);
						native_set_fpucw (regs.fpcr);
					}
					if (extra & 0x0800)
						set_fpsr (m68k_dreg (regs, opcode & 7));
					if (extra & 0x0400)
						regs.fpiar = m68k_dreg (regs, opcode & 7);
				}
			} else if ((opcode & 0x38) == 0x08) {
				if (fault_if_no_fpu (opcode, extra, 0, pc))
					return;
				if (extra & 0x2000) {
					if (extra & 0x1000)
						m68k_areg (regs, opcode & 7) = regs.fpcr & 0xffff;
					if (extra & 0x0800)
						m68k_areg (regs, opcode & 7) = fpp_get_fpsr ();
					if (extra & 0x0400)
						m68k_areg (regs, opcode & 7) = regs.fpiar;
				} else {
					if (extra & 0x1000) {
						regs.fpcr = m68k_areg (regs, opcode & 7);
						native_set_fpucw (regs.fpcr);
					}
					if (extra & 0x0800)
						set_fpsr (m68k_areg (regs, opcode & 7));
					if (extra & 0x0400)
						regs.fpiar = m68k_areg (regs, opcode & 7);
				}
			} else if ((opcode & 0x3f) == 0x3c) {
				if (fault_if_no_fpu (opcode, extra, 0, pc))
					return;
				if ((extra & 0x2000) == 0) {
					uae_u32 ext[3];
					// 68060 FMOVEM.L #imm,more than 1 control register: unimplemented EA
					uae_u16 bits = extra & (0x1000 | 0x0800 | 0x0400);
					if (bits && bits != 0x1000 && bits != 0x0800 && bits != 0x400) {
						if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
							return;
					}
					// fetch first, use only after all data has been fetched
					ext[0] = ext[1] = ext[2] = 0;
					if (extra & 0x1000)
						ext[0] = x_cp_next_ilong ();
					if (extra & 0x0800)
						ext[1] = x_cp_next_ilong ();
					if (extra & 0x0400)
						ext[2] = x_cp_next_ilong ();
					if (extra & 0x1000) {
						regs.fpcr = ext[0];
						native_set_fpucw (regs.fpcr);
					}
					if (extra & 0x0800)
						set_fpsr (ext[1]);
					if (extra & 0x0400)
						regs.fpiar = ext[2];
				}
			} else if (extra & 0x2000) {
				/* FMOVEM FPP->memory */
				uae_u32 ad;
				int incr = 0;

				if (get_fp_ad (opcode, &ad) == 0) {
					fpu_noinst (opcode, pc);
					return;
				}
				if (fault_if_no_fpu (opcode, extra, ad, pc))
					return;

				if ((opcode & 0x38) == 0x20) {
					if (extra & 0x1000)
						incr += 4;
					if (extra & 0x0800)
						incr += 4;
					if (extra & 0x0400)
						incr += 4;
				}
				ad -= incr;
				if (extra & 0x1000) {
					x_cp_put_long (ad, regs.fpcr & 0xffff);
					ad += 4;
				}
				if (extra & 0x0800) {
					x_cp_put_long (ad, fpp_get_fpsr ());
					ad += 4;
				}
				if (extra & 0x0400) {
					x_cp_put_long (ad, regs.fpiar);
					ad += 4;
				}
				ad -= incr;
				if ((opcode & 0x38) == 0x18)
					m68k_areg (regs, opcode & 7) = ad;
				if ((opcode & 0x38) == 0x20)
					m68k_areg (regs, opcode & 7) = ad;
			} else {
				/* FMOVEM memory->FPP */
				uae_u32 ad;
				int incr = 0;

				if (get_fp_ad (opcode, &ad) == 0) {
					fpu_noinst (opcode, pc);
					return;
				}
				if (fault_if_no_fpu (opcode, extra, ad, pc))
					return;

				if((opcode & 0x38) == 0x20) {
					if (extra & 0x1000)
						incr += 4;
					if (extra & 0x0800)
						incr += 4;
					if (extra & 0x0400)
						incr += 4;
					ad = ad - incr;
				}
				if (extra & 0x1000) {
					regs.fpcr = x_cp_get_long (ad);
					native_set_fpucw (regs.fpcr);
					ad += 4;
				}
				if (extra & 0x0800) {
					set_fpsr (x_cp_get_long (ad));
					ad += 4;
				}
				if (extra & 0x0400) {
					regs.fpiar = x_cp_get_long (ad);
					ad += 4;
				}
				if ((opcode & 0x38) == 0x18)
					m68k_areg (regs, opcode & 7) = ad;
				if ((opcode & 0x38) == 0x20)
					m68k_areg (regs, opcode & 7) = ad - incr;
			}
			return;

		case 6:
		case 7:
			{
				uae_u32 ad, list = 0;
				int incr = 1;
				int regdir = 1;
				if (get_fp_ad (opcode, &ad) == 0) {
					fpu_noinst (opcode, pc);
					return;
				}
				if (fault_if_no_fpu (opcode, extra, ad, pc))
					return;
				switch ((extra >> 11) & 3)
				{
					case 0:	/* static pred */
						list = extra & 0xff;
						regdir = -1;
						break;
					case 1:	/* dynamic pred */
						if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
							return;
						list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
						regdir = -1;
						break;
					case 2:	/* static postinc */
						list = extra & 0xff;
						break;
					case 3:	/* dynamic postinc */
						if (fault_if_60 (opcode, extra, ad, pc, FPU_EXP_UNIMP_EA))
							return;
						list = m68k_dreg (regs, (extra >> 4) & 3) & 0xff;
						break;
				}
				if ((opcode & 0x38) == 0x20) // -(an)
					incr = -1;
				if (extra & 0x2000) {
					/* FMOVEM FPP->memory */
					ad = fmovem2mem (ad, list, incr, regdir);
				} else {
					/* FMOVEM memory->FPP */
					ad = fmovem2fpp (ad, list, incr, regdir);
				}
				if ((opcode & 0x38) == 0x18 || (opcode & 0x38) == 0x20)
					m68k_areg (regs, opcode & 7) = ad;
			}
			return;

		case 0:
		case 2: /* Extremely common */
			regs.fpiar = pc;
			reg = (extra >> 7) & 7;
			if ((extra & 0xfc00) == 0x5c00) {
				if (fault_if_no_fpu (opcode, extra, 0, pc))
					return;
				if (fault_if_unimplemented_680x0 (opcode, extra, ad, pc, &srcd, reg))
					return;
				CLEAR_STATUS ();
				if (!fpu_get_constant(&regs.fp[reg], extra)) {
					fpu_noinst(opcode, pc);
					return;
				}
				MAKE_FPSR (&regs.fp[reg].fp);
				return;
			}

			// 6888x does not have special exceptions, check immediately
			if (fault_if_unimplemented_6888x (opcode, extra, pc))
				return;

			v = get_fp_value (opcode, extra, &srcd, pc, &ad);
			if (v <= 0) {
				if (v == 0)
					fpu_noinst (opcode, pc);
				return;
			}

			// get_fp_value() checked this, but only if EA was nonzero (non-register)
			if (fault_if_unimplemented_680x0 (opcode, extra, ad, pc, &srcd, reg))
				return;

			regs.fpiar =  pc;

			CLEAR_STATUS ();
#ifdef WITH_SOFTFLOAT
			if (currprefs.fpu_softfloat)
				v = arithmetic_softfloat(&srcd.fpx, reg, extra);
			else
#endif
				v = arithmetic_fp(srcd.fp, reg, extra);
			if (!v)
				fpu_noinst (opcode, pc);
			return;
		default:
		break;
	}
	fpu_noinst (opcode, pc);
}

void fpuop_arithmetic (uae_u32 opcode, uae_u16 extra)
{
	regs.fpu_state = 1;
	regs.fp_exception = false;
	fpu_mmu_fixup = false;
	fpuop_arithmetic2 (opcode, extra);
	if (fpu_mmu_fixup) {
		mmufixup[0].reg = -1;
	}
#ifdef WITH_SOFTFLOAT
	if (currprefs.fpu_softfloat) {
		// Any exception status bit and matching exception enable bits set?
		if ((regs.fpcr >> 8) & (regs.fpsr >> 8)) {
			uae_u32 mask = regs.fpcr >> 8;
			int vector = 0;
			for (int i = 7; i >= 0; i--) {
				if (mask & (1 << i)) {
					if (i > 0)
						i--;
					vector = i + 48;
					break;
				}
			}
			// logging only so far
			write_log (_T("FPU exception: %08x %d!\n"), regs.fpsr, vector);
		}
	}
#endif
}

void fpu_reset (void)
{
	regs.fpcr = regs.fpsr = regs.fpiar = 0;
	regs.fpu_exp_state = 0;
	fpset (&regs.fp_result, 1);
	native_set_fpucw (regs.fpcr);
	fpux_restore (NULL);

#ifdef WITH_SOFTFLOAT
	fxsizes[0] = int32_to_floatx80(-128);
	fxsizes[1] = int32_to_floatx80(127);
	fxsizes[2] = int32_to_floatx80(-32768);
	fxsizes[3] = int32_to_floatx80(32767);
	fxsizes[4] = int32_to_floatx80(-2147483648);
	fxsizes[5] = int32_to_floatx80(2147483647);
	fxzero = int32_to_floatx80(0);
	fx_1e0 = int32_to_floatx80(1);
	fx_1e1 = int32_to_floatx80(10);
	fx_1e2 = int32_to_floatx80(100);
	fx_1e4 = int32_to_floatx80(10000);
	fx_1e8 = int32_to_floatx80(100000000);
#endif
}

uae_u8 *restore_fpu (uae_u8 *src)
{
	uae_u32 w1, w2, w3;
	int i;
	uae_u32 flags;

	changed_prefs.fpu_model = currprefs.fpu_model = restore_u32 ();
	flags = restore_u32 ();
	for (i = 0; i < 8; i++) {
		w1 = restore_u16 () << 16;
		w2 = restore_u32 ();
		w3 = restore_u32 ();
		to_exten (&regs.fp[i], w1, w2, w3);
	}
	regs.fpcr = restore_u32 ();
	native_set_fpucw (regs.fpcr);
	regs.fpsr = restore_u32 ();
	regs.fpiar = restore_u32 ();
	if (flags & 0x80000000) {
		restore_u32 ();
		restore_u32 ();
	}
	if (flags & 0x40000000) {
		w1 = restore_u16() << 16;
		w2 = restore_u32();
		w3 = restore_u32();
		to_exten(&regs.exp_src1, w1, w2, w3);
		w1 = restore_u16() << 16;
		w2 = restore_u32();
		w3 = restore_u32();
		to_exten(&regs.exp_src2, w1, w2, w3);
		regs.exp_pack[0] = restore_u32();
		regs.exp_pack[1] = restore_u32();
		regs.exp_pack[2] = restore_u32();
		regs.exp_opcode = restore_u16();
		regs.exp_extra = restore_u16();
		regs.exp_type = restore_u16();
	}
	regs.fpu_state = (flags & 1) ? 0 : 1;
	regs.fpu_exp_state = (flags & 2) ? 1 : 0;
	if (flags & 4)
		regs.fpu_exp_state = 2;
	write_log(_T("FPU: %d\n"), currprefs.fpu_model);
	return src;
}

uae_u8 *save_fpu (int *len, uae_u8 *dstptr)
{
	uae_u32 w1, w2, w3;
	uae_u8 *dstbak, *dst;
	int i;

	*len = 0;
#ifndef WINUAE_FOR_HATARI
        /* Under Hatari, we save all FPU variables, even if fpu_model==0 */
	if (currprefs.fpu_model == 0)
		return 0;
#endif
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 4+4+8*10+4+4+4+4+4+2*10+3*(4+2));
	save_u32 (currprefs.fpu_model);
	save_u32 (0x80000000 | 0x40000000 | (regs.fpu_state == 0 ? 1 : 0) | (regs.fpu_exp_state ? 2 : 0) | (regs.fpu_exp_state > 1 ? 4 : 0));
	for (i = 0; i < 8; i++) {
		from_exten (&regs.fp[i], &w1, &w2, &w3);
		save_u16 (w1 >> 16);
		save_u32 (w2);
		save_u32 (w3);
	}
	save_u32 (regs.fpcr);
	save_u32 (regs.fpsr);
	save_u32 (regs.fpiar);

	save_u32 (-1);
	save_u32 (0);

	from_exten(&regs.exp_src1, &w1, &w2, &w3);
	save_u16(w1 >> 16);
	save_u32(w2);
	save_u32(w3);
	from_exten(&regs.exp_src2, &w1, &w2, &w3);
	save_u16(w1 >> 16);
	save_u32(w2);
	save_u32(w3);
	save_u32(regs.exp_pack[0]);
	save_u32(regs.exp_pack[1]);
	save_u32(regs.exp_pack[2]);
	save_u16(regs.exp_opcode);
	save_u16(regs.exp_extra);
	save_u16(regs.exp_type);

	*len = dst - dstbak;
	return dstbak;
}

#ifdef _MSC_VER
#pragma fenv_access(off)
#endif
