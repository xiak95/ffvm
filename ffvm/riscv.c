#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>

#ifdef WIN32
#pragma warning(disable:4996) // disable warnings
#endif

typedef unsigned long long uint64_t;
typedef long long           int64_t;
typedef unsigned           uint32_t;
typedef int                 int32_t;
typedef unsigned short     uint16_t;
typedef short               int16_t;
typedef unsigned char      uint8_t;
typedef char                int8_t;
#define get_tick_count GetTickCount
#define usleep(t)      Sleep((t)/1000)

typedef struct {
    uint32_t x[32];
    uint32_t pc;
    uint64_t f[32];
    uint32_t fcsr;
    #define MAX_MEM_SIZE (64 * 1024 * 1024)
    uint8_t  mem[MAX_MEM_SIZE];
    uint32_t heap;
    #define TS_EXIT (1 << 0)
    uint32_t status;
} RISCV;

static uint8_t riscv_memr8(RISCV *riscv, uint32_t addr)
{
    return *(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
}

static void riscv_memw8(RISCV *riscv, uint32_t addr, uint8_t data)
{
    *(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
}

static uint16_t riscv_memr16(RISCV *riscv, uint32_t addr)
{
    if ((addr & 0x1) == 0) {
        return *(uint16_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
    } else {
        return (riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] << 0)
             | (riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] << 8);
    }
}

static void riscv_memw16(RISCV *riscv, uint32_t addr, uint16_t data)
{
    if ((addr & 0x1) == 0) {
        *(uint16_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
    } else {
        riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 0);
        riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 8);
    }
}

static uint32_t riscv_memr32(RISCV *riscv, uint32_t addr)
{
    switch (addr) {
    case 0xF0000000: return fgetc(stdin);
    }
    if ((addr & 0x3) == 0) {
        return *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1)));
    } else {
        return (riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] << 0)
             | (riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] << 8)
             | (riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] <<16)
             | (riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] <<24);
    }
}

static void riscv_memw32(RISCV *riscv, uint32_t addr, uint32_t data)
{
    switch (addr) {
    case 0xF0000000: return;
    case 0xF0000004: fputc(data, stdout); return;
    case 0xF0000008: fputc(data, stderr); return;
    }
    if ((addr & 0x3) == 0) {
        *(uint32_t*)(riscv->mem + (addr & (MAX_MEM_SIZE - 1))) = data;
    } else {
        riscv->mem[(addr + 0) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 0);
        riscv->mem[(addr + 1) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >> 8);
        riscv->mem[(addr + 2) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>16);
        riscv->mem[(addr + 3) & (MAX_MEM_SIZE - 1)] = (uint8_t)(data >>24);
    }
}

static int32_t signed_extend(uint32_t a, int size)
{
    return (a & (1 << (size - 1))) ? (a | ~((1 << size) - 1)) : a;
}

static uint32_t handle_ecall(RISCV *riscv)
{
    switch (riscv->x[17]) {
    case 93: riscv->status |= TS_EXIT; return 0; //sys_exit
    default: return 0;
    }
}

static void riscv_execute_rv16(RISCV *riscv, uint16_t instruction)
{
    const uint16_t inst_opcode = (instruction >> 0) & 0x3;
    const uint16_t inst_rd     = (instruction >> 7) & 0x1f;
    const uint16_t inst_rs1    = (instruction >> 7) & 0x1f;
    const uint16_t inst_rs2    = (instruction >> 2) & 0x1f;
    const uint16_t inst_rs1s   = (instruction >> 7) & 0x7;
    const uint16_t inst_rs2s   = (instruction >> 2) & 0x7;
    const uint16_t inst_rds    = (instruction >> 2) & 0x7;
    const uint16_t inst_imm6   =((instruction >> 2) & 0x1f) | ((instruction >> 7) & (1 << 5));
    const uint16_t inst_imm7   =((instruction >> 4) & (1 << 2)) | ((instruction >> 7) & (0x7 << 3)) | ((instruction << 1) & (1 << 6));
    const uint16_t inst_imm8   =((instruction >> 7) & (0x7 << 3)) | ((instruction << 1) & (0x3 << 6));
    const uint16_t inst_imm9   =((instruction >> 2) & (0x3 << 3)) | ((instruction >> 7) & (1 << 5)) | ((instruction << 4) & (0x7 << 6));
    const uint16_t inst_imm10  =((instruction >> 4) & (1 << 2)) | ((instruction >> 2) & (1 << 3)) | ((instruction >> 7) & (0x3 << 4)) | ((instruction >> 1) & (0x7 << 6));
    const uint16_t inst_imm12  =((instruction >> 2) & (0x007 << 1)) | ((instruction >> 7) & (1 << 4)) | ((instruction << 3) & (1 << 5 ))
                               |((instruction >> 1) & (0x80d << 6)) | ((instruction << 1) & (1 << 7)) | ((instruction << 2) & (1 << 10));
    const uint16_t inst_imm18  =((instruction << 5) & (1 << 17)) | ((instruction << 10) & (0x1f << 12));
    const uint16_t inst_funct2 = (instruction >>10) & 0x3;
    const uint16_t inst_funct3 = (instruction >>13) & 0x7;
    uint32_t bflag = 0, temp;
    switch (inst_opcode) {
    case 0:
        switch (inst_funct3) {
        case 0: riscv->x[8 + inst_rds] = riscv->x[2] + inst_imm10; break; // c.addi4spn
        case 1: // c.fld
            riscv->f[8 + inst_rds] = (uint64_t)riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 0) << 0 ;
            riscv->f[8 + inst_rds]|= (uint64_t)riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 4) << 32;
            break;
        case 2: riscv->x[8 + inst_rds] = riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7); break; // c.lw
        case 3: riscv->f[8 + inst_rds] = riscv_memr32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7); break; // c.flw
        case 5: // c.fsd
            riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 0, (uint32_t)(riscv->f[8 + inst_rs2s] >> 0 ));
            riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm8 + 4, (uint32_t)(riscv->f[8 + inst_rs2s] >> 32));
            break;
        case 6: riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7, riscv->x[8 + inst_rs2s]); break; // c.sw
        case 7: riscv_memw32(riscv, riscv->x[8 + inst_rs1s] + inst_imm7, (uint32_t)riscv->f[8 + inst_rs2s]); break; // c.fsw
        }
        break;
    case 1:
        switch (inst_funct3) {
        case 0: riscv->x[inst_rd] += signed_extend(inst_imm6, 6); break; // c.addi
        case 1: // c.jal
            riscv->x[1] = riscv->pc + 2;
            riscv->pc  += signed_extend(inst_imm12, 12);
            bflag = 1;
            break;
        case 2: riscv->x[inst_rd] = signed_extend(inst_imm6, 6); break; // c.li
        case 3:
            if (inst_rd == 2) { // c.addi16sp
                temp = ((instruction >> 2) & (1 << 4)) | ((instruction << 3) & (1 << 5)) | ((instruction << 1) & (1 << 6))
                     | ((instruction << 4) & (0x3 << 7)) | ((instruction >> 3) & (1 << 9));
                riscv->x[inst_rd] += signed_extend(temp, 10);
            } else { // c.lui
                riscv->x[inst_rd]  = signed_extend(inst_imm18, 18);
            }
            break;
        case 4:
            switch (inst_funct2) {
            case 0: riscv->x[8 + inst_rds] = (uint32_t)riscv->x[8 + inst_rds] >> inst_imm6; break; // c.srli
            case 1: riscv->x[8 + inst_rds] = (int32_t )riscv->x[8 + inst_rds] >> inst_imm6; break; // c.srai
            case 2: riscv->x[8 + inst_rds]+= signed_extend(inst_imm6, 6); break; // c.andi
            case 3:
                switch ((instruction >> 5) & 3) {
                case 0: riscv->x[8 + inst_rds] -= riscv->x[8 + inst_rs2s]; break; // c.sub
                case 1: riscv->x[8 + inst_rds] ^= riscv->x[8 + inst_rs2s]; break; // c.xor
                case 2: riscv->x[8 + inst_rds] |= riscv->x[8 + inst_rs2s]; break; // c.or
                case 3: riscv->x[8 + inst_rds] &= riscv->x[8 + inst_rs2s]; break; // c.and
                }
                break;
            }
        case 5: riscv->pc += signed_extend(inst_imm12, 12); bflag = 1; break; // c.j
        case 6: // c.beqz
        case 7: // c.bnez
            if (inst_funct3 == 6 && riscv->x[8 + inst_rs1s] == 0 || inst_funct3 == 7 && riscv->x[8 + inst_rs1s] != 0) {
                temp = ((instruction >> 2) & (0x3 << 1)) | ((instruction >> 7) & (0x3 << 3)) | ((instruction << 3) & (1 << 5))
                     | ((instruction << 1) & (0x3 << 6)) | ((instruction > 4) & (1 << 8));
                riscv->pc += signed_extend(temp, 9);
                bflag = 1;
            }
            break;
        }
        break;
    case 2:
        switch (inst_funct3) {
        case 0: riscv->x[inst_rd] <<= inst_imm6; break; // c.slli
        case 1: // c.fldsp
            riscv->f[inst_rd]  = (uint64_t)riscv_memr32(riscv, riscv->x[2] + inst_imm9 + 0) << 0 ;
            riscv->f[inst_rd] |= (uint64_t)riscv_memr32(riscv, riscv->x[2] + inst_imm9 + 4) << 32;
            break;
        case 2: // c.lwsp
        case 3: // c.flwsp
            temp = ((instruction >> 2) & (0x7 << 2)) | ((instruction >> 7) & (1 << 5)) | ((instruction << 4) & (0x3 << 6));
            if (inst_funct3 == 2) riscv->x[inst_rd] = riscv_memr32(riscv, riscv->x[2] + temp); // c.lwsp
            else                  riscv->f[inst_rd] = riscv_memr32(riscv, riscv->x[2] + temp); // c.flwsp
            break;
        case 4:
            if ((instruction & (1 << 12)) == 0) {
                if (inst_rs2 == 0) { // c.jr
                    riscv->pc = riscv->x[inst_rs1]; bflag = 1;
                } else { // c.mv
                    riscv->x[inst_rd] = riscv->x[inst_rs2];
                }
            } else {
                if (inst_rs1 == 0 && inst_rs2 == 0) { // c.ebreak;
                } else if (inst_rs2 == 0) { // c.jalr
                    temp        = riscv->pc + 2;
                    riscv->pc   = riscv->x[inst_rs1];
                    riscv->x[1] = temp;
                    bflag       = 1;
                } else { // c.add
                    riscv->x[inst_rd] += riscv->x[inst_rs2];
                }
            }
            break;
        case 5: // c.fsdsp
            temp = ((instruction >> 7) & (0x7 << 3)) | ((instruction >> 1) & (0x7 << 6));
            riscv_memw32(riscv, riscv->x[2] + temp + 0, (uint32_t)(riscv->f[inst_rs2] >> 0 ));
            riscv_memw32(riscv, riscv->x[2] + temp + 4, (uint32_t)(riscv->f[inst_rs2] >> 32));
            break;
        case 6: // c.swsp
            temp = ((instruction >> 7) & (0x7 << 3)) | ((instruction >> 1) & (0x7 << 6));
            riscv_memw32(riscv, riscv->x[2] + temp, (uint32_t)riscv->x[inst_rs2]);
            break;
        case 7: // c.fswsp
            temp = ((instruction >> 7) & (0x7 << 3)) | ((instruction >> 1) & (0x7 << 6));
            riscv_memw32(riscv, riscv->x[2] + temp, (uint32_t)riscv->f[inst_rs2]);
            break;
        }
        break;
    }
    riscv->pc += bflag ? 0 : 2;
}

static void riscv_execute_rv32(RISCV *riscv, uint32_t instruction)
{
    const uint32_t inst_opcode = (instruction >> 0) & 0x7f;
    const uint32_t inst_rd     = (instruction >> 7) & 0x1f;
    const uint32_t inst_funct3 = (instruction >>12) & 0x07;
    const uint32_t inst_rs1    = (instruction >>15) & 0x1f;
    const uint32_t inst_rs2    = (instruction >>20) & 0x1f;
    const uint32_t inst_funct7 = (instruction >>25) & 0x7f;
    const uint32_t inst_imm12i = (instruction >>20) & 0xfff;
    const uint32_t inst_imm12s =((instruction >>20) & (0x7f << 5)) | ((instruction >> 7) & 0x1f);
    const uint32_t inst_imm13b =((instruction >>19) & (0x1  <<12)) | ((instruction << 4) & (0x1 << 11))
                               |((instruction >>20) & (0x3f << 5)) | ((instruction >> 7) & (0xf <<  1));
    const uint32_t inst_imm20u = instruction & (0xfffff << 12);
    const uint32_t inst_imm21j =((instruction >> 11) & (1 << 20)) | (instruction & (0xff << 12))
                               |((instruction >> 9 ) & (1 << 11)) | ((instruction >> 20) & (0x3ff << 1));
    uint32_t bflag = 0, maddr, temp;
    int64_t  mult64res;

    switch (inst_opcode) {
    case 0x37: riscv->x[inst_rd] = inst_imm20u; break; // u-type lui
    case 0x17: riscv->x[inst_rd] = riscv->pc + (int32_t)inst_imm20u; break; // u-type auipc
    case 0x6f: // j-type jal
        riscv->x[inst_rd] = riscv->pc + 4;
        riscv->pc += signed_extend(inst_imm21j, 21);
        bflag = 1;
        break;
    case 0x67: // i-type
        switch (inst_funct3) {
        case 0x0: // jalr
            temp = riscv->pc + 4;
            riscv->pc = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12);
            riscv->pc&=~(1 << 0);
            riscv->x[inst_rd] = temp;
            bflag = 1;
            break;
        }
        break;
    case 0x63: // b-type
        switch (inst_funct3) {
        case 0x0: bflag = riscv->x[inst_rs1] == riscv->x[inst_rs2]; break; // beq
        case 0x1: bflag = riscv->x[inst_rs1] != riscv->x[inst_rs2]; break; // bne
        case 0x4: bflag = (int32_t)riscv->x[inst_rs1] <  (int32_t)riscv->x[inst_rs2]; break; // blt
        case 0x5: bflag = (int32_t)riscv->x[inst_rs1] >= (int32_t)riscv->x[inst_rs2]; break; // bge
        case 0x6: bflag = riscv->x[inst_rs1] <  riscv->x[inst_rs2]; break; // bltu
        case 0x7: bflag = riscv->x[inst_rs1] >= riscv->x[inst_rs2]; break; // bgeu
        }
        if (bflag) riscv->pc += signed_extend(inst_imm13b, 13);
        break;
    case 0x03: // i-type
        maddr = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12);
        switch (inst_funct3) {
        case 0x0: riscv->x[inst_rd] = (int32_t)riscv_memr8 (riscv, maddr); break; // lb
        case 0x1: riscv->x[inst_rd] = (int32_t)riscv_memr16(riscv, maddr); break; // lh
        case 0x2: riscv->x[inst_rd] = riscv_memr32(riscv, maddr); break; // lw
        case 0x4: riscv->x[inst_rd] = riscv_memr8 (riscv, maddr); break; // lbu
        case 0x5: riscv->x[inst_rd] = riscv_memr16(riscv, maddr); break; // lhu
        }
        break;
    case 0x23: // s-type
        maddr = riscv->x[inst_rs1] + signed_extend(inst_imm12s, 12);
        switch (inst_funct3) {
        case 0x0: riscv_memw8 (riscv, maddr, (uint8_t )riscv->x[inst_rs2]); break; // sb
        case 0x1: riscv_memw16(riscv, maddr, (uint16_t)riscv->x[inst_rs2]); break; // sh
        case 0x2: riscv_memw32(riscv, maddr, riscv->x[inst_rs2]); break; // sw
        }
        break;
    case 0x13: // i-type
        switch (inst_funct3) {
        case 0x0: riscv->x[inst_rd] = riscv->x[inst_rs1] + signed_extend(inst_imm12i, 12); break; // addi
        case 0x2: riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] < signed_extend(inst_imm12i, 12);  break; // slti
        case 0x3: riscv->x[inst_rd] = riscv->x[inst_rs1] < (uint32_t)signed_extend(inst_imm12i, 12); break; // sltiu
        case 0x4: riscv->x[inst_rd] = riscv->x[inst_rs1] ^ (signed_extend(inst_imm12i, 12)); break; // xori
        case 0x6: riscv->x[inst_rd] = riscv->x[inst_rs1] | (signed_extend(inst_imm12i, 12)); break; // ori
        case 0x7: riscv->x[inst_rd] = riscv->x[inst_rs1] & (signed_extend(inst_imm12i, 12)); break; // andi
        case 0x1: riscv->x[inst_rd] = riscv->x[inst_rs1] << (inst_imm12i & 0x1f); break; // slli
        case 0x5: // srli & srai
            if (inst_funct7 & (1 << 5)) { // srai
                riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] >> (inst_imm12i & 0x1f);
            } else { // srli
                riscv->x[inst_rd] = riscv->x[inst_rs1] >> (inst_imm12i & 0x1f);
            }
            break;
        }
        break;
    case 0x33: // r-type
        if ((inst_funct7 & (1 << 0)) == 0) {
            switch (inst_funct3) {
            case 0x0: // add & sub
                if (inst_funct7 & (1 << 5)) { // sub
                    riscv->x[inst_rd] = riscv->x[inst_rs1] - riscv->x[inst_rs2];
                } else { // add
                    riscv->x[inst_rd] = riscv->x[inst_rs1] + riscv->x[inst_rs2];
                }
                break;
            case 0x1: riscv->x[inst_rd] = riscv->x[inst_rs1] << (riscv->x[inst_rs2] & 0x1f); break; // sll
            case 0x2: riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] < (int32_t)riscv->x[inst_rs2]; break; // slt
            case 0x3: riscv->x[inst_rd] = riscv->x[inst_rs1] < riscv->x[inst_rs2]; break; // sltu
            case 0x4: riscv->x[inst_rd] = riscv->x[inst_rs1] ^ riscv->x[inst_rs2]; break; // xor
            case 0x5: // srl & sra
                if (inst_funct7 & (1 << 5)) { // sra
                    riscv->x[inst_rd] = (int32_t)riscv->x[inst_rs1] >> (riscv->x[inst_rs2] & 0x1f);
                } else { // srl
                    riscv->x[inst_rd] = riscv->x[inst_rs1] >> (riscv->x[inst_rs2] & 0x1f);
                }
                break;
            case 0x6: riscv->x[inst_rd] = riscv->x[inst_rs1] | riscv->x[inst_rs2]; break; // or
            case 0x7: riscv->x[inst_rd] = riscv->x[inst_rs1] & riscv->x[inst_rs2]; break; // and
            }
        } else {
            switch (inst_funct3) {
            case 0x0: riscv->x[inst_rd] = riscv->x[inst_rs1] * riscv->x[inst_rs2]; break; // mul
            case 0x1: // mulh
                mult64res = (int64_t)riscv->x[inst_rs1] * (int64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x2: // mulhsu
                mult64res = (int64_t)riscv->x[inst_rs1] * (uint64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x3: // mulhu
                mult64res = (uint64_t)riscv->x[inst_rs1] * (uint64_t)riscv->x[inst_rs2];
                riscv->x[inst_rd] = (uint32_t)(mult64res >> 32);
                break;
            case 0x4: // div
                riscv->x[inst_rd] = (uint32_t)((int32_t)riscv->x[inst_rs1] / (int32_t)riscv->x[inst_rs2]);
                break;
            case 0x5: // divu
                riscv->x[inst_rd] = riscv->x[inst_rs1] / riscv->x[inst_rs2];
                break;
            case 0x6: // rem
                riscv->x[inst_rd] = (uint32_t)((int32_t)riscv->x[inst_rs1] % (int32_t)riscv->x[inst_rs2]);
                break;
            case 0x7: // remu
                riscv->x[inst_rd] = riscv->x[inst_rs1] % riscv->x[inst_rs2];
                break;
            }
        }
        break;
    case 0x73:
        if (instruction & (1 << 20)) { // ebreak;
        } else { // ecall
            riscv->x[10] = handle_ecall(riscv);
        }
        break;
    }
    riscv->pc += bflag ? 0 : 4;
}

void riscv_run(RISCV *riscv)
{
    const uint32_t instruction = riscv_memr32(riscv, riscv->pc);
    if ((instruction & 0x3) != 0x3) {
        riscv_execute_rv16(riscv, (uint16_t)instruction);
    } else {
        riscv_execute_rv32(riscv, (uint32_t)instruction);
    }
    riscv->x[0] = 0;
}

#define RISCV_CPU_FREQ  (1*1000*1000)
#define RISCV_FRAMERATE  50

int main(int argc, char *argv[])
{
    char romfile[MAX_PATH] = "rom.bin";
    uint32_t next_tick = 0;
    int32_t  sleep_tick= 0;
    RISCV    *riscv = NULL;
    FILE     *fp    = NULL;
    int      i;

    riscv = calloc(1, sizeof(RISCV));
    if (!riscv) return 0;

    if (argc >= 2) {
        strncpy(romfile, argv[1], sizeof(romfile));
    }

    riscv->pc = 0x80000000; // startup addr
    fp = fopen(romfile, "rb");
    if (fp) {
        fread(riscv->mem, 1, sizeof(riscv->mem), fp);
        fclose(fp);
    }

    while (!(riscv->status & (TS_EXIT))) {
        if (!next_tick) next_tick = get_tick_count();
        next_tick += 1000 / RISCV_FRAMERATE;
        for (i=0; i<RISCV_CPU_FREQ/RISCV_FRAMERATE; i++) {
            riscv_run(riscv);
        }
        sleep_tick = next_tick - get_tick_count();
        if (sleep_tick > 0) usleep(sleep_tick * 1000);
//      printf("sleep_tick: %d\n", sleep_tick);
    }

    free(riscv);
    getch();
    return 0;
}
