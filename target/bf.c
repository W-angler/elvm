#include <assert.h>
#include <stdbool.h>

#include <ir/ir.h>
#include <target/util.h>

typedef struct {
  int mp;
  int loop_ptr;
  int ifzero_cnt;
  int ifzero_off[4];
  int ifzero_omp[4];
} BFGen;

static BFGen bf;

static const int BF_RUNNING = 0;
static const int BF_PC = 2;
static const int BF_NPC = 8;
static const int BF_A = 14;
static const int BF_B = 20;
static const int BF_C = 26;
static const int BF_D = 32;
static const int BF_BP = 38;
static const int BF_SP = 44;
static const int BF_OP = 50;

static const int BF_DBG = 58;

static const int BF_WRK = 60;

static const int BF_LOAD_REQ = 67;
static const int BF_STORE_REQ = 68;

static const int BF_MEM = 70;
static const int BF_MEM_V = 0;
static const int BF_MEM_A = 3;
static const int BF_MEM_WRK = 6;
static const int BF_MEM_USE = 12;
#define BF_MEM_CTL_LEN 15
static const int BF_MEM_BLK_LEN = (256*3) + BF_MEM_CTL_LEN;

static void bf_emit(const char* s) {
  fputs(s, stdout);
}

static void bf_comment(const char* s) {
  printf("\n# %s\n", s);
}

static void bf_rep(char c, int n) {
  for (int i = 0; i < n; i++)
    putchar(c);
}

static void bf_set_ptr(int ptr) {
  bf.mp = ptr;
}

static void bf_move_ptr(int ptr) {
  if (ptr > bf.mp) {
    bf_rep('>', ptr - bf.mp);
  } else {
    bf_rep('<', bf.mp - ptr);
  }
  bf.mp = ptr;
}

#if 0
static void bf_move_neg(int from, int to, char c) {
  bf_move_ptr(from);
  bf_emit("[-");
  bf_move_ptr(to);
  putchar('-');
  bf_move_ptr(from);
  bf_emit("]");
}
#endif

static void bf_move(int from, int to) {
  bf_move_ptr(from);
  bf_emit("[-");
  bf_move_ptr(to);
  putchar('+');
  bf_move_ptr(from);
  bf_emit("]");
}

static void bf_move2(int from, int to, int to2) {
  bf_move_ptr(from);
  bf_emit("[-");
  bf_move_ptr(to);
  putchar('+');
  bf_move_ptr(to2);
  putchar('+');
  bf_move_ptr(from);
  bf_emit("]");
}

static void bf_move_word(int from, int to) {
  bf_move(from-1, to-1);
  bf_move(from, to);
  bf_move(from+1, to+1);
}

static void bf_move_word2(int from, int to, int to2) {
  bf_move2(from-1, to-1, to2-1);
  bf_move2(from, to, to2);
  bf_move2(from+1, to+1, to2+1);
}

#if 0
static void bf_copy(int from, int to, int wrk) {
  bf_move2(from, to, wrk);
  bf_move(wrk, from);
}
#endif

static void bf_copy_word(int from, int to, int wrk) {
  bf_move_word2(from, to, wrk);
  bf_move_word(wrk, from);
}

static void bf_add(int ptr, int v) {
  bf_move_ptr(ptr);
#ifdef __eir__
  v %= 256;
#else
  v &= 255;
#endif
  if (v > 127) {
    bf_rep('-', 256 - v);
  } else {
    bf_rep('+', v);
  }
}

static void bf_add_word(int ptr, int v) {
  bf_add(ptr - 1, v / 65536);
  bf_add(ptr, v / 256 % 256);
  bf_add(ptr + 1, v % 256);
}

static void bf_clear(int ptr) {
  bf_move_ptr(ptr);
  bf_emit("[-]");
}

static void bf_clear_word(int ptr) {
  bf_clear(ptr-1);
  bf_clear(ptr);
  bf_clear(ptr+1);
}

static int bf_regpos(int r) {
  switch (r) {
  case A: return BF_A;
  case B: return BF_B;
  case C: return BF_C;
  case D: return BF_D;
  case BP: return BF_BP;
  case SP: return BF_SP;
  default:
    error("unknown reg: %d", r);
  }
}

static void bf_dbg(const char* s) {
  for (; *s; s++) {
    bf_clear(BF_DBG);
    bf_add(BF_DBG, *s);
    bf_emit(".");
  }
  bf_clear(BF_DBG);
}

static void bf_interpreter_check() {
  bf_comment("interpreter check");

  // Test for cell wrap != 256
  emit_line(">[-]<[-]++++++++[>++++++++<-]>[<++++>-]<[>>");

  // Print message 'Sorry this program needs an 8bit interpreter\n'
  emit_line(">++++[<++++>-]<+[>++++++>+++++++>++>+++>+++++<<<<<-]>>>>>--.<<<-");
  emit_line("-------.+++..+++++++.>--.<-----.<++.+.>-.>.<---.++.---.<--.>+++.");
  emit_line("<------.>-----.>.<+.<++++..-.>+++++.>.<<---.>-----.>.>+++++.<<<+");
  emit_line(".>-----.+++++++++++.>.<<+++++++.+++++.>.<---------.>--.--.++.<.>");
  emit_line("++.<.>--.<<++++++++++.");

  // endif
  emit_line("<<[-]]");

  // Test for cell wrap != 256 and flip condition
  emit_line(">[-]<[-]++++++++[>++++++++<-]>[<++++>-]+<[>-<[-]]>[-<");
}

static void bf_init_state(Data* data) {
  bf_interpreter_check();

  bf_comment("init data");
  for (int mp = 0; data; data = data->next, mp++) {
    assert(mp < 65536);
    if (data->v) {
      int hi = mp / 256;
      int lo = mp % 256;
      int ptr = BF_MEM + BF_MEM_BLK_LEN * hi + BF_MEM_CTL_LEN + lo * 3;
      bf_add_word(ptr, data->v);
    }
  }
}

static void bf_loop_begin(int ptr, char c) {
  bf_move_ptr(ptr);
  bf_emit("[");
  if (c)
    putchar(c);
  bf.loop_ptr = ptr;
}

static void bf_loop_end() {
  bf_move_ptr(bf.loop_ptr);
  bf_emit("]");
}

static void bf_ifzero_begin_impl(int off, bool reset, const char* ifnz) {
  bf.ifzero_omp[bf.ifzero_cnt] = bf.mp;
  bf.mp = 0;

  bf_add(off * 2, -1);
  bf_move_ptr(0);
  bf_emit("[");
  if (reset)
    bf_emit("[-]");
  bf_emit(ifnz);
  bf_move_ptr(off);
  bf_emit("]");
  bf_move_ptr(off * 2);
  bf_emit("+[-");

  bf.mp = bf.ifzero_omp[bf.ifzero_cnt] + off;
  bf.ifzero_off[bf.ifzero_cnt] = off;
  bf.ifzero_cnt++;
}

static void bf_ifzero_begin(int off) {
  bf_ifzero_begin_impl(off, false, "");
}

static void bf_ifzero_end() {
  bf.ifzero_cnt--;
  int off = bf.ifzero_off[bf.ifzero_cnt];
  int omp = bf.ifzero_omp[bf.ifzero_cnt];

  bf_move_ptr(omp + off);

  bf_set_ptr(0);
  bf_move_ptr(off);
  bf_emit("+]");

  bf.mp = omp + off * 2;
}

static void bf_emit_add(Inst* inst) {
  int dst = bf_regpos(inst->dst.reg);
  if (inst->src.type == REG) {
    int src = bf_regpos(inst->src.reg);
    if (src == dst)
      error("TODO?");
    bf_copy_word(src, BF_WRK, BF_WRK+3);
  } else {
    bf_add_word(BF_WRK, inst->src.imm);
  }

  // Add BF_WRK to dst.
  bf_loop_begin(BF_WRK+1, '-'); {
    // Increment.
    bf_move_ptr(dst+1);
    bf_emit("+");
    // Carry?
    bf_ifzero_begin(1); {
      bf_add(dst, 1);
      bf_move_ptr(dst);
      bf_ifzero_begin(2); {
        bf_add(dst-1, 1);
      }; bf_ifzero_end();
    }; bf_ifzero_end();
  }; bf_loop_end();

  bf_loop_begin(BF_WRK, '-'); {
    // Increment.
    bf_move_ptr(dst);
    bf_emit("+");
    // Carry?
    bf_ifzero_begin(2); {
      bf_add(dst-1, 1);
    }; bf_ifzero_end();
  }; bf_loop_end();
  bf_move(BF_WRK-1, dst-1);
}

static void bf_emit_op(Inst* inst) {
  switch (inst->op) {
  case MOV: {
    int dst = bf_regpos(inst->dst.reg);
    if (inst->src.type == REG) {
      int src = bf_regpos(inst->src.reg);
      if (src != dst) {
        bf_clear_word(dst);
        bf_copy_word(src, dst, BF_WRK);
      }
    } else {
      bf_clear_word(dst);
      bf_add_word(dst, inst->src.imm);
    }
    break;
  }

  case ADD:
    bf_emit_add(inst);
    break;

  case SUB:
  case LOAD:
  case STORE:
  case EQ:
  case NE:
  case LT:
  case GT:
  case LE:
  case GE:
    error("oops etc");
    break;

  case PUTC:
    if (inst->src.type == REG) {
      int src = bf_regpos(inst->src.reg);
      bf_move_ptr(src + 1);
      bf_emit(".");
    } else {
      bf_add(BF_WRK, inst->src.imm % 256);
      bf_emit(".");
      bf_clear(BF_WRK);
    }
    break;

  case GETC:
    error("oops IO");
    break;
  case EXIT:
    bf_clear(BF_RUNNING);
    break;

  case DUMP:
    break;
  case JEQ:
  case JNE:
  case JLT:
  case JGT:
  case JLE:
  case JGE:
    error("oops jcc");
    break;

  case JMP:
    bf_add_word(BF_NPC, inst->jmp.imm - 1);
    break;

  default:
    error("oops");
  }
}

void bf_emit_code(Inst* inst) {
  bf_comment("fetch pc");
  bf_move_word2(BF_PC, BF_NPC, BF_OP);

  bf_comment("increment pc");
  bf_move_ptr(BF_NPC+3);
  bf_emit("-");
  bf_move_ptr(BF_NPC+1);
  bf_emit("+");
  bf_emit("[>]>+");
  // if 0
  bf_emit("[-<<+>>>+]");
  bf_set_ptr(BF_NPC+3);

  for (int pc_h = 0; pc_h < 256; pc_h++) {
    printf("\n# pc_h=%d\n", pc_h);

    bf_add(BF_OP-2, -1);
    bf_move_ptr(BF_OP);
    bf_emit("[<]<+[-<+");
    bf_set_ptr(BF_OP-2);

    for (int pc_l = 0; pc_l < 256; pc_l++) {
      int pc = pc_h * 256 + pc_l;
      if (!inst)
        break;

      bf_add(BF_OP+3, -1);
      bf_move_ptr(BF_OP+1);
      bf_emit("[>]>+[->+");
      bf_set_ptr(BF_OP+3);

      printf("\n# pc_l=%d\n", pc_l);

      for (; inst && inst->pc == pc; inst = inst->next) {
        printf("\n# ");
        dump_inst_fp(inst, stdout);

        if (0) {
          bf_emit("@");
          bf_dbg(format("%d pc=%d\n", inst->op, pc));
        }

        bf_emit_op(inst);
      }

      bf_move_ptr(BF_OP+3);
      bf_emit("]");
      bf_add(BF_OP+1, -1);
    }

    bf_move_ptr(BF_OP-2);
    bf_emit("]");
    bf_add(BF_OP, -1);
  }

  bf_clear_word(BF_OP);
}

void target_bf(Module* module) {
  bf_init_state(module->data);

  bf_comment("prologue");
  bf_add(BF_RUNNING, 1);
  emit_line("[");

  bf_emit_code(module->text);

  bf_move_word(BF_NPC, BF_PC);

  bf_comment("epilogue");
  bf_move_ptr(BF_RUNNING);
  emit_line("]");

  // endif for cell size check. (should be [-]] but we already have a zero)
  emit_line("]");
  // EOL at EOF
  emit_line("[...THE END...]");
}
