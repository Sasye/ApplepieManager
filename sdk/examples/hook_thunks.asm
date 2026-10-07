; PE-backed native entry thunks. No live game or runtime code generation.
EXTERN AphTestThunkBody:PROC
PUBLIC AphTestNearJump, AphTestShortJump, AphTestIndirectJump
PUBLIC AphTestChangedJump, AphTestDuplicateJump
.const
thunkPointer DQ AphTestThunkBody
.code
AphTestNearJump PROC
    jmp AphTestThunkBody
    db 16 dup(90h)
AphTestNearJump ENDP
AphTestShortJump PROC
    jmp SHORT shortBody
    db 14 dup(90h)
shortBody:
    jmp AphTestThunkBody
    db 16 dup(90h)
AphTestShortJump ENDP
AphTestIndirectJump PROC
    jmp QWORD PTR [thunkPointer]
    db 16 dup(90h)
AphTestIndirectJump ENDP
AphTestChangedJump PROC
    jmp AphTestThunkBody
    db 16 dup(90h)
AphTestChangedJump ENDP
AphTestDuplicateJump PROC
    jmp AphTestThunkBody
    db 16 dup(90h)
AphTestDuplicateJump ENDP
END
