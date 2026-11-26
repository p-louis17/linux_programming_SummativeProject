; ============================================================================
; sensor_stats.asm
; Assembler : NASM (Intel syntax)      Platform: Linux x86-64, raw syscalls
; Purpose   : Read sensor_readings.txt, validate each line as an integer in
;             0..100, then print count, sum, min and max statistics.
;
; Register map (kept alive for the whole run; syscall only clobbers rax,rcx,r11)
;   r12 = total records        r13 = valid count      r14 = invalid count
;   r15 = sum of valid values
;   rbx = value of the record being parsed (built digit by digit)
;   r8  = number of digits seen in the current record
;   r9  = "bad" flag for the current record (1 = invalid character or > 100)
;   r10 = "has content" flag (1 = at least one non CR/LF char seen on this line)
;   rsi = pointer to next character in buffer, rcx = characters left in chunk
; ============================================================================

global _start

SYS_READ   equ 0
SYS_WRITE  equ 1
SYS_OPEN   equ 2
SYS_CLOSE  equ 3
SYS_EXIT   equ 60
BUFSZ      equ 4096

section .data
fname        db "sensor_readings.txt", 0
msg_total    db "Total records: ", 0
msg_valid    db "Valid measurements: ", 0
msg_invalid  db "Invalid measurements: ", 0
msg_sum      db "Sum of valid measurements: ", 0
msg_min      db "Minimum measurement: ", 0
msg_max      db "Maximum measurement: ", 0
msg_na       db "N/A", 0
msg_openerr  db "Error: cannot open sensor_readings.txt", 10, 0
msg_readerr  db "Error: failed while reading sensor_readings.txt", 10, 0
newline      db 10, 0
min_val      dq 101              ; larger than any valid value, so first valid replaces it
max_val      dq 0                ; smaller than or equal to any valid value

section .bss
fd           resq 1
buffer       resb BUFSZ
numbuf       resb 24             ; scratch space for number to text conversion

section .text

_start:
    ; ---- open(fname, O_RDONLY, 0) ------------------------------------------
    mov     eax, SYS_OPEN
    lea     rdi, [rel fname]
    xor     esi, esi             ; O_RDONLY = 0
    xor     edx, edx
    syscall
    test    rax, rax
    js      open_error           ; negative return value = -errno
    mov     [rel fd], rax

    ; clear parser state
    xor     r12d, r12d
    xor     r13d, r13d
    xor     r14d, r14d
    xor     r15d, r15d
    xor     ebx, ebx
    xor     r8d, r8d
    xor     r9d, r9d
    xor     r10d, r10d

; ---- read the file in chunks; parser state carries across chunk borders ----
read_loop:
    mov     eax, SYS_READ
    mov     rdi, [rel fd]
    lea     rsi, [rel buffer]
    mov     edx, BUFSZ
    syscall
    test    rax, rax
    js      read_error           ; negative = read failed
    jz      end_of_file          ; 0 = no more data
    mov     rcx, rax             ; rcx = bytes in this chunk
    lea     rsi, [rel buffer]    ; rsi = current character pointer

; ---- character by character traversal --------------------------------------
next_char:
    movzx   eax, byte [rsi]      ; load one character
    inc     rsi
    cmp     al, 10               ; LF ends a record (works for LF and CRLF)
    je      line_end
    cmp     al, 13               ; CR is ignored, so CRLF behaves like LF
    je      char_done
    mov     r10d, 1              ; real content seen on this line
    cmp     al, '0'
    jb      bad_char             ; below '0' (this also catches '-')
    cmp     al, '9'
    ja      bad_char             ; above '9'
    test    r9, r9
    jnz     char_done            ; record already invalid, keep skipping
    sub     eax, '0'             ; ASCII digit -> 0..9
    imul    rbx, rbx, 10         ; value = value * 10
    add     rbx, rax             ;         + digit
    inc     r8                   ; one more digit
    cmp     rbx, 100
    jbe     char_done            ; still within range
    mov     r9d, 1               ; above 100: mark invalid (also stops overflow)
    jmp     char_done
bad_char:
    mov     r9d, 1               ; non numeric character: record is invalid
    jmp     char_done
line_end:
    call    finish_record
char_done:
    dec     rcx
    jnz     next_char            ; more characters in this chunk
    jmp     read_loop            ; chunk done, read the next one

end_of_file:
    test    r10, r10             ; last line without a trailing newline?
    jz      close_file
    call    finish_record

close_file:
    mov     eax, SYS_CLOSE
    mov     rdi, [rel fd]
    syscall

; ---- print results ---------------------------------------------------------
    lea     rsi, [rel msg_total]
    call    print_cstr
    mov     rax, r12
    call    print_uint

    lea     rsi, [rel msg_valid]
    call    print_cstr
    mov     rax, r13
    call    print_uint

    lea     rsi, [rel msg_invalid]
    call    print_cstr
    mov     rax, r14
    call    print_uint

    lea     rsi, [rel msg_sum]
    call    print_cstr
    mov     rax, r15
    call    print_uint

    lea     rsi, [rel msg_min]
    call    print_cstr
    test    r13, r13             ; no valid measurements: min/max undefined
    jz      print_min_na
    mov     rax, [rel min_val]
    call    print_uint
    jmp     do_max
print_min_na:
    lea     rsi, [rel msg_na]
    call    print_cstr
    lea     rsi, [rel newline]
    call    print_cstr

do_max:
    lea     rsi, [rel msg_max]
    call    print_cstr
    test    r13, r13
    jz      print_max_na
    mov     rax, [rel max_val]
    call    print_uint
    jmp     exit_ok
print_max_na:
    lea     rsi, [rel msg_na]
    call    print_cstr
    lea     rsi, [rel newline]
    call    print_cstr

exit_ok:
    mov     eax, SYS_EXIT
    xor     edi, edi             ; status 0
    syscall

open_error:
    lea     rsi, [rel msg_openerr]
    call    print_cstr
    mov     edi, 1
    jmp     exit_code
read_error:
    mov     eax, SYS_CLOSE       ; release the file before leaving
    mov     rdi, [rel fd]
    syscall
    lea     rsi, [rel msg_readerr]
    call    print_cstr
    mov     edi, 2
exit_code:
    mov     eax, SYS_EXIT
    syscall

; ----------------------------------------------------------------------------
; finish_record: called when a line ends (or at EOF with pending content).
; Counts the record as valid or invalid, updates sum/min/max, resets the
; per-record state. Empty lines have r8 = 0 and are therefore invalid.
; ----------------------------------------------------------------------------
finish_record:
    inc     r12                  ; total++
    test    r9, r9
    jnz     .invalid             ; bad character or out of range
    test    r8, r8
    jz      .invalid             ; no digits: empty line
    inc     r13                  ; valid++
    add     r15, rbx             ; sum += value
    cmp     rbx, [rel min_val]
    jae     .check_max           ; value >= min: no change
    mov     [rel min_val], rbx
.check_max:
    cmp     rbx, [rel max_val]
    jbe     .reset               ; value <= max: no change
    mov     [rel max_val], rbx
    jmp     .reset
.invalid:
    inc     r14                  ; invalid++
.reset:
    xor     ebx, ebx             ; clear value, digit count, flags
    xor     r8d, r8d
    xor     r9d, r9d
    xor     r10d, r10d
    ret

; ----------------------------------------------------------------------------
; print_cstr: write the zero terminated string at rsi to stdout.
; ----------------------------------------------------------------------------
print_cstr:
    mov     rdx, rsi
.len:
    cmp     byte [rdx], 0
    je      .write
    inc     rdx
    jmp     .len
.write:
    sub     rdx, rsi             ; rdx = length
    mov     eax, SYS_WRITE
    mov     edi, 1
    syscall
    ret

; ----------------------------------------------------------------------------
; print_uint: print the unsigned number in rax followed by a newline.
; Repeatedly divides by 10; remainders are digits, produced last to first,
; so they are stored backwards from the end of numbuf.
; ----------------------------------------------------------------------------
print_uint:
    lea     rdi, [rel numbuf + 23]
    mov     byte [rdi], 10       ; trailing newline
    mov     r11d, 10
.digit:
    xor     edx, edx
    div     r11                  ; rax = quotient, rdx = remainder
    add     dl, '0'
    dec     rdi
    mov     [rdi], dl
    test    rax, rax
    jnz     .digit
    lea     rdx, [rel numbuf + 24]
    sub     rdx, rdi             ; length including newline
    mov     rsi, rdi
    mov     eax, SYS_WRITE
    mov     edi, 1
    syscall
    ret