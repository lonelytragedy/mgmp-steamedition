
.data
PUBLIC mgmp_ver_table
mgmp_ver_table QWORD 17 DUP(0)

.code
mgmp_fwd_GetFileVersionInfoA PROC
    jmp QWORD PTR [mgmp_ver_table + 0]
mgmp_fwd_GetFileVersionInfoA ENDP

mgmp_fwd_GetFileVersionInfoByHandle PROC
    jmp QWORD PTR [mgmp_ver_table + 8]
mgmp_fwd_GetFileVersionInfoByHandle ENDP

mgmp_fwd_GetFileVersionInfoExA PROC
    jmp QWORD PTR [mgmp_ver_table + 16]
mgmp_fwd_GetFileVersionInfoExA ENDP

mgmp_fwd_GetFileVersionInfoExW PROC
    jmp QWORD PTR [mgmp_ver_table + 24]
mgmp_fwd_GetFileVersionInfoExW ENDP

mgmp_fwd_GetFileVersionInfoSizeA PROC
    jmp QWORD PTR [mgmp_ver_table + 32]
mgmp_fwd_GetFileVersionInfoSizeA ENDP

mgmp_fwd_GetFileVersionInfoSizeExA PROC
    jmp QWORD PTR [mgmp_ver_table + 40]
mgmp_fwd_GetFileVersionInfoSizeExA ENDP

mgmp_fwd_GetFileVersionInfoSizeExW PROC
    jmp QWORD PTR [mgmp_ver_table + 48]
mgmp_fwd_GetFileVersionInfoSizeExW ENDP

mgmp_fwd_GetFileVersionInfoSizeW PROC
    jmp QWORD PTR [mgmp_ver_table + 56]
mgmp_fwd_GetFileVersionInfoSizeW ENDP

mgmp_fwd_GetFileVersionInfoW PROC
    jmp QWORD PTR [mgmp_ver_table + 64]
mgmp_fwd_GetFileVersionInfoW ENDP

mgmp_fwd_VerFindFileA PROC
    jmp QWORD PTR [mgmp_ver_table + 72]
mgmp_fwd_VerFindFileA ENDP

mgmp_fwd_VerFindFileW PROC
    jmp QWORD PTR [mgmp_ver_table + 80]
mgmp_fwd_VerFindFileW ENDP

mgmp_fwd_VerInstallFileA PROC
    jmp QWORD PTR [mgmp_ver_table + 88]
mgmp_fwd_VerInstallFileA ENDP

mgmp_fwd_VerInstallFileW PROC
    jmp QWORD PTR [mgmp_ver_table + 96]
mgmp_fwd_VerInstallFileW ENDP

mgmp_fwd_VerLanguageNameA PROC
    jmp QWORD PTR [mgmp_ver_table + 104]
mgmp_fwd_VerLanguageNameA ENDP

mgmp_fwd_VerLanguageNameW PROC
    jmp QWORD PTR [mgmp_ver_table + 112]
mgmp_fwd_VerLanguageNameW ENDP

mgmp_fwd_VerQueryValueA PROC
    jmp QWORD PTR [mgmp_ver_table + 120]
mgmp_fwd_VerQueryValueA ENDP

mgmp_fwd_VerQueryValueW PROC
    jmp QWORD PTR [mgmp_ver_table + 128]
mgmp_fwd_VerQueryValueW ENDP

END
