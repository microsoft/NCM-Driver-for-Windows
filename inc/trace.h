// Copyright (C) Microsoft Corporation. All rights reserved.

#pragma once

#ifdef _KERNEL_MODE
#define CODE_SEG(segment) __declspec(code_seg(segment))
#else
#define CODE_SEG(segment)
#endif

/// Use on pageable functions.
#define PAGED CODE_SEG("PAGE") _IRQL_always_function_max_(PASSIVE_LEVEL)
/// Use on pageable functions, where you don't want the SAL IRQL annotation to say PASSIVE_LEVEL.
#define PAGEDX CODE_SEG("PAGE")
/// Use on code in the INIT segment. (Code is discarded after DriverEntry returns.)
#define INITCODE CODE_SEG("INIT")
/// Use on code that must always be locked in memory.
#define NONPAGED CODE_SEG(".text") _IRQL_requires_max_(DISPATCH_LEVEL)
/// Use on code that must always be locked in memory, where you don't want the SAL IRQL annotation to say DISPATCH_LEVEL.
#define NONPAGEDX CODE_SEG(".text")

//
// Define the tracing flags.
//
// Tracing GUID - 3fdb277b-ca73-4cf2-915f-a1f2f7b6af4c
//

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(USBNCM, (3fdb277b, ca73, 4cf2, 915f, a1f2f7b6af4c), \
    WPP_DEFINE_BIT(USBNCM_ALL)  \
    WPP_DEFINE_BIT(USBNCM_FUNCTION)  \
    WPP_DEFINE_BIT(USBNCM_HOST)  \
    WPP_DEFINE_BIT(USBNCM_ADAPTER))

#define WPP_LEVEL_FLAGS_LOGGER(level,flags)   WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(level,flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

// begin_wpp config
// USEPREFIX(FuncEntry, "%!STDPREFIX![%!FUNC!] -->");
// FUNC FuncEntry{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS);
// USEPREFIX(FuncExit, "%!STDPREFIX![%!FUNC!] <-- exit=0x%x",EXP);
// FUNC FuncExit{LEVEL=TRACE_LEVEL_VERBOSE}(FLAGS, EXP);
// FUNC TraceError{ LEVEL = TRACE_LEVEL_ERROR }(FLAGS, MSG, ...);
// FUNC TraceWarn{ LEVEL = TRACE_LEVEL_WARNING }(FLAGS, MSG, ...);
// FUNC TraceInfo{ LEVEL = TRACE_LEVEL_INFORMATION }(FLAGS, MSG, ...);
// FUNC TraceVerbose{ LEVEL = TRACE_LEVEL_VERBOSE }(FLAGS, MSG, ...);
// end_wpp

#define WPP_LEVEL_FLAGS_EXP_LOGGER(lvl,flags, EXP) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_EXP_ENABLED(lvl,flags, EXP) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//For WPP Inflight Trace Recorder
#define WPP_RECORDER_LEVEL_FLAGS_EXP_FILTER(lvl, FLAGS, EXP)   WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, FLAGS)
#define WPP_RECORDER_LEVEL_FLAGS_EXP_ARGS(lvl, FLAGS, EXP)     WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, FLAGS)

// Workaround to capture verbose trace if the verbose level is turned on by WPP file session. 
// Without this macro, IFR and WPP file session only log errors, warnings, and informational events. You need set HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\<serviceName>\Parameters
//  VerboseOn
//  Data type
//  REG_DWORD
//  Description
//  By default (0), Setting this value to 1 adds the verbose output to get logged.
// WDF team has a task to solve this issue in RS5, it can be removed once that task is done
#define WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags) WPP_CONTROL(WPP_BIT_ ## flags).AutoLogContext, lvl, WPP_BIT_ ## flags
#define WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl,flags) (lvl < TRACE_LEVEL_VERBOSE || WPP_CONTROL(WPP_BIT_ ## flags).AutoLogVerboseEnabled || WPP_LEVEL_FLAGS_ENABLED(lvl, flags))

#define MACRO_START do {
#define MACRO_END } while(0)

//
// WPP Macros: NCM_RETURN_IF_NOT_NT_SUCCESS_MSG
//
// begin_wpp config
// FUNC NCM_RETURN_IF_NOT_NT_SUCCESS_MSG{COMPNAME=USBNCM_ALL,LEVEL=TRACE_LEVEL_ERROR}(NTEXPR,MSG,...);
// USEPREFIX (NCM_RETURN_IF_NOT_NT_SUCCESS_MSG, "%!STDPREFIX! !! UsbNcm - %!FUNC!: ");
// USESUFFIX (NCM_RETURN_IF_NOT_NT_SUCCESS_MSG, " [status=%!STATUS!]", nt__wpp);
// end_wpp

#define WPP_COMPNAME_LEVEL_NTEXPR_PRE(comp,level,ntexpr) MACRO_START NTSTATUS nt__wpp = (ntexpr); if (!NT_SUCCESS(nt__wpp)) {
#define WPP_COMPNAME_LEVEL_NTEXPR_POST(comp,level,ntexpr) ; return nt__wpp; } MACRO_END
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR_FILTER(comp,level,ntexpr) WPP_RECORDER_LEVEL_FLAGS_FILTER(level, comp)
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR_ARGS(comp,level,ntexpr) WPP_RECORDER_LEVEL_FLAGS_ARGS(level, comp)

// WPP Macros: NCM_RETURN_IF_NOT_NT_SUCCESS
//
// begin_wpp config
// FUNC NCM_RETURN_IF_NOT_NT_SUCCESS{COMPNAME=USBNCM_ALL,LEVEL=TRACE_LEVEL_ERROR}(NTEXPR2);
// USEPREFIX (NCM_RETURN_IF_NOT_NT_SUCCESS, "%!STDPREFIX! !! UsbNcm - %!FUNC!: ");
// USESUFFIX (NCM_RETURN_IF_NOT_NT_SUCCESS, " [status=%!STATUS!]", nt__wpp);
// end_wpp

#define WPP_COMPNAME_LEVEL_NTEXPR2_PRE(comp, level, ntexpr) MACRO_START NTSTATUS nt__wpp = (ntexpr); if (!NT_SUCCESS(nt__wpp)) {
#define WPP_COMPNAME_LEVEL_NTEXPR2_POST(comp, level, ntexpr); return nt__wpp; } MACRO_END
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR2_FILTER(comp, level, ntexpr) WPP_RECORDER_LEVEL_FLAGS_FILTER(level, comp)
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR2_ARGS(comp, level, ntexpr) WPP_RECORDER_LEVEL_FLAGS_ARGS(level, comp)

//
// WPP Macros: NCM_LOG_IF_NOT_NT_SUCCESS_MSG
//
// begin_wpp config
// FUNC NCM_LOG_IF_NOT_NT_SUCCESS_MSG{COMPNAME=USBNCM_ALL,LEVEL=TRACE_LEVEL_WARNING}(NTEXPR3,MSG,...);
// USEPREFIX (NCM_LOG_IF_NOT_NT_SUCCESS_MSG, "%!STDPREFIX!  ! UsbNcm - %ws: ", nt__function);
// USESUFFIX (NCM_LOG_IF_NOT_NT_SUCCESS_MSG, " [status=%!STATUS!]", nt__wpp);
// end_wpp

#define WPP_COMPNAME_LEVEL_NTEXPR3_PRE(comp,level,ntexpr) [&](PCWSTR nt__function) { NTSTATUS nt__wpp = (ntexpr); if (!NT_SUCCESS(nt__wpp)) {
#define WPP_COMPNAME_LEVEL_NTEXPR3_POST(comp,level,ntexpr) ; } return nt__wpp; }(__FUNCTIONW__)
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR3_FILTER(comp,level,ntexpr) WPP_RECORDER_LEVEL_FLAGS_FILTER(level, comp)
#define WPP_RECORDER_COMPNAME_LEVEL_NTEXPR3_ARGS(comp,level,ntexpr) WPP_RECORDER_LEVEL_FLAGS_ARGS(level, comp)

//
// WPP Macros: NCM_RETURN_NT_STATUS_IF_FALSE_MSG
//
// begin_wpp config
// FUNC NCM_RETURN_NT_STATUS_IF_FALSE_MSG{COMPNAME=USBNCM_ALL,LEVEL=TRACE_LEVEL_ERROR}(CONDITION,STATUS,MSG,...);
// USEPREFIX (NCM_RETURN_NT_STATUS_IF_FALSE_MSG, "%!STDPREFIX! !! UsbNcm - %!FUNC!: ");
// USESUFFIX (NCM_RETURN_NT_STATUS_IF_FALSE_MSG, "%!s! is FALSE", #CONDITION);
// end_wpp

#define WPP_COMPNAME_LEVEL_CONDITION_STATUS_PRE(comp,level,condition,status) MACRO_START if (!(condition)) {
#define WPP_COMPNAME_LEVEL_CONDITION_STATUS_POST(comp,level,condition,status) ; return status; } MACRO_END
#define WPP_RECORDER_COMPNAME_LEVEL_CONDITION_STATUS_FILTER(comp,level,condition,status) WPP_RECORDER_LEVEL_FLAGS_FILTER(level, comp)
#define WPP_RECORDER_COMPNAME_LEVEL_CONDITION_STATUS_ARGS(comp,level,condition,status) WPP_RECORDER_LEVEL_FLAGS_ARGS(level, comp)
