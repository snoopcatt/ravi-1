/******************************************************************************
 * Copyright (C) 2020-2021 Dibyendu Majumdar
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ******************************************************************************/

/*
 * Convert the linear IR to C code that can be compiled and
 * executed by Ravi VM.
 */

#include "codegen.h"
#include "ravi_api.h"

#include <assert.h>
#include <stddef.h>

/*
 * Only 64-bits supported right now
 * Following must be kept in sync with changes in the actual header files
 */

static const char Lua_header[] =
    "#ifdef __MIRC__\n"
    "typedef __SIZE_TYPE__ size_t;\n"
    "typedef __PTRDIFF_TYPE__ ptrdiff_t;\n"
    "typedef __INTPTR_TYPE__ intptr_t;\n"
    "typedef __INT64_TYPE__ int64_t;\n"
    "typedef __UINT64_TYPE__ uint64_t;\n"
    "typedef __INT32_TYPE__ int32_t;\n"
    "typedef __UINT32_TYPE__ uint32_t;\n"
    "typedef __INT16_TYPE__ int16_t;\n"
    "typedef __UINT16_TYPE__ uint16_t;\n"
    "typedef __INT8_TYPE__ int8_t;\n"
    "typedef __UINT8_TYPE__ uint8_t;\n"
    "#define NULL ((void *)0)\n"
    "#define EXPORT\n"
    "#else\n"
    "#include <stddef.h>\n"
    "#include <stdint.h>\n"
    "#ifdef _WIN32\n"
    "#define EXPORT __declspec(dllexport)\n"
    "#else\n"
    "#define EXPORT\n"
    "#endif\n"
    "#endif\n"
    "typedef size_t lu_mem;\n"
    "typedef unsigned char lu_byte;\n"
    "typedef uint16_t LuaType;\n"
    "typedef struct lua_State lua_State;\n"
    "#define LUA_TNONE		(-1)\n"
    "#define LUA_TNIL		0\n"
    "#define LUA_TBOOLEAN		1\n"
    "#define LUA_TLIGHTUSERDATA	2\n"
    "#define LUA_TNUMBER		3\n"
    "#define LUA_TSTRING		4\n"
    "#define LUA_TTABLE		5\n"
    "#define LUA_TFUNCTION		6\n"
    "#define LUA_TUSERDATA		7\n"
    "#define LUA_TTHREAD		8\n"
    "#define LUA_OK  0\n"
    "typedef enum {TM_INDEX,TM_NEWINDEX,TM_GC,\n"
    "	TM_MODE,TM_LEN,TM_EQ,TM_ADD,TM_SUB,TM_MUL,\n"
    "	TM_MOD,TM_POW,TM_DIV,TM_IDIV,TM_BAND,TM_BOR,\n"
    "	TM_BXOR,TM_SHL,TM_SHR,TM_UNM,TM_BNOT,TM_LT,\n"
    "	TM_LE,TM_CONCAT,TM_CALL,TM_N\n"
    "} TMS;\n"
    "typedef double lua_Number;\n"
    "typedef int64_t lua_Integer;\n"
    "typedef uint64_t lua_Unsigned;\n"
    "typedef int (*lua_CFunction) (lua_State *L);\n"
    "typedef union {\n"
    "	lua_Number n;\n"
    "	double u;\n"
    "	void *s;\n"
    "	lua_Integer i;\n"
    "	long l;\n"
    "} L_Umaxalign;\n"
    "#define lua_assert(c)		((void)0)\n"
    "#define check_exp(c,e)		(e)\n"
    "#define lua_longassert(c)	((void)0)\n"
    "#define luai_apicheck(l,e)	lua_assert(e)\n"
    "#define api_check(l,e,msg)	luai_apicheck(l,(e) && msg)\n"
    "#define UNUSED(x)	((void)(x))\n"
    "#define cast(t, exp)	((t)(exp))\n"
    "#define cast_void(i)	cast(void, (i))\n"
    "#define cast_byte(i)	cast(lu_byte, (i))\n"
    "#define cast_num(i)	cast(lua_Number, (i))\n"
    "#define cast_int(i)	cast(int, (i))\n"
    "#define cast_uchar(i)	cast(unsigned char, (i))\n"
    "#define l_castS2U(i)	((lua_Unsigned)(i))\n"
    "#define l_castU2S(i)	((lua_Integer)(i))\n"
    "#define l_noret		void\n"
    "typedef unsigned int Instruction;\n"
    "#define luai_numidiv(L,a,b)     ((void)L, l_floor(luai_numdiv(L,a,b)))\n"
    "#define luai_numdiv(L,a,b)      ((a)/(b))\n"
    "#define luai_nummod(L,a,b,m)  \\\n"
    "  { (m) = l_mathop(fmod)(a,b); if ((m)*(b) < 0) (m) += (b); }\n"
    "#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))\n"
    "#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))\n"
    "#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))\n"
    "#define RAVI_TFCF	(LUA_TFUNCTION | (4 << 4))\n"
    "#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))\n"
    "#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))\n"
    "#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))\n"
    "#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))\n"
    "#define RAVI_TIARRAY (LUA_TTABLE | (1 << 4))\n"
    "#define RAVI_TFARRAY (LUA_TTABLE | (2 << 4))\n"
    "#define BIT_ISCOLLECTABLE	(1 << 15)\n"
    "#define ctb(t)			((t) | BIT_ISCOLLECTABLE)\n"
    "typedef struct GCObject GCObject;\n"
    "#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked\n"
    "struct GCObject {\n"
    "  CommonHeader;\n"
    "};\n"
    "typedef union Value {\n"
    "  GCObject *gc;\n"
    "  void *p;\n"
    "  int b;\n"
    "  lua_CFunction f;\n"
    "  lua_Integer i;\n"
    "  lua_Number n;\n"
    "} Value;\n"
    "#define TValuefields	Value value_; LuaType tt_\n"
    "typedef struct lua_TValue {\n"
    "  TValuefields;\n"
    "} TValue;\n"
    "#define NILCONSTANT	{NULL}, LUA_TNIL\n"
    "#define val_(o)		((o)->value_)\n"
    "#define rttype(o)	((o)->tt_)\n"
    "#define novariant(x)	((x) & 0x0F)\n"
    "#define ttype(o)	(rttype(o) & 0x7F)\n"
    "#define ttnov(o)	(novariant(rttype(o)))\n"
    "#define checktag(o,t)		(rttype(o) == (t))\n"
    "#define checktype(o,t)		(ttnov(o) == (t))\n"
    "#define ttisnumber(o)		checktype((o), LUA_TNUMBER)\n"
    "#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)\n"
    "#define ttisinteger(o)		checktag((o), LUA_TNUMINT)\n"
    "#define ttisnil(o)		checktag((o), LUA_TNIL)\n"
    "#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)\n"
    "#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)\n"
    "#define ttisstring(o)		checktype((o), LUA_TSTRING)\n"
    "#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))\n"
    "#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))\n"
    "#define ttistable(o)		checktype((o), LUA_TTABLE)\n"
    "#define ttisiarray(o)    checktag((o), ctb(RAVI_TIARRAY))\n"
    "#define ttisfarray(o)    checktag((o), ctb(RAVI_TFARRAY))\n"
    "#define ttisarray(o)     (ttisiarray(o) || ttisfarray(o))\n"
    "#define ttisLtable(o)    checktag((o), ctb(LUA_TTABLE))\n"
    "#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)\n"
    "#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)\n"
    "#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))\n"
    "#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))\n"
    "#define ttislcf(o)		checktag((o), LUA_TLCF)\n"
    "#define ttisfcf(o) (ttype(o) == RAVI_TFCF)\n"
    "#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))\n"
    "#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))\n"
    "#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)\n"
    "#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)\n"
    "#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)\n"
    "#define nvalue(o)	check_exp(ttisnumber(o), \\\n"
    "	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))\n"
    "#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)\n"
    "#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)\n"
    "#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))\n"
    "#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))\n"
    "#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))\n"
    "#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))\n"
    "#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))\n"
    "#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)\n"
    "#define fcfvalue(o) check_exp(ttisfcf(o), val_(o).p)\n"
    "#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))\n"
    "#define arrvalue(o) check_exp(ttisarray(o), gco2array(val_(o).gc))\n"
    "#define arrvalue(o) check_exp(ttisarray(o), gco2array(val_(o).gc))\n"
    "#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)\n"
    "#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))\n"
    "#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))\n"
    "#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))\n"
    "#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)\n"
    "#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)\n"
    "#define checkliveness(L,obj) \\\n"
    "	lua_longassert(!iscollectable(obj) || \\\n"
    "		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))\n"
    "#define settt_(o,t)	((o)->tt_=(t))\n"
    "#define setfltvalue(obj,x) \\\n"
    "  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }\n"
    "#define chgfltvalue(obj,x) \\\n"
    "  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }\n"
    "#define setivalue(obj,x) \\\n"
    "  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }\n"
    "#define chgivalue(obj,x) \\\n"
    "  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }\n"
    "#define setnilvalue(obj) settt_(obj, LUA_TNIL)\n"
    "#define setfvalue(obj,x) \\\n"
    "  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }\n"
    "#define setfvalue_fastcall(obj, x, tag) \\\n"
    "{ \\\n"
    "    TValue *io = (obj);   \\\n"
    "    lua_assert(tag >= 1 && tag < 0x80); \\\n"
    "    val_(io).p = (x);     \\\n"
    "    settt_(io, ((tag << 8) | RAVI_TFCF)); \\\n"
    "}\n"
    "#define setpvalue(obj,x) \\\n"
    "  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }\n"
    "#define setbvalue(obj,x) \\\n"
    "  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }\n"
    "#define setgcovalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); GCObject *i_g=(x); \\\n"
    "    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }\n"
    "#define setsvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); TString *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setuvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); Udata *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setthvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); lua_State *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setclLvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); LClosure *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setclCvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); CClosure *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define sethvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); Table *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setiarrayvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); Table *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(RAVI_TIARRAY)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setfarrayvalue(L,obj,x) \\\n"
    "  { TValue *io = (obj); Table *x_ = (x); \\\n"
    "    val_(io).gc = obj2gco(x_); settt_(io, ctb(RAVI_TFARRAY)); \\\n"
    "    checkliveness(L,io); }\n"
    "#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)\n"
    "#define setobj(L,obj1,obj2) \\\n"
    // NOTE we cannot use aggregate assign so following assigns by field but assumes
    // n covers all value types
    "	{ TValue *io1=(obj1); const TValue *io2=(obj2); io1->tt_ = io2->tt_; val_(io1).n = val_(io2).n; \\\n"
    "	  (void)L; checkliveness(L,io1); }\n"
    "#define setobjs2s	setobj\n"
    "#define setobj2s	setobj\n"
    "#define setsvalue2s	setsvalue\n"
    "#define sethvalue2s	sethvalue\n"
    "#define setptvalue2s	setptvalue\n"
    "#define setobjt2t	setobj\n"
    "#define setobj2n	setobj\n"
    "#define setsvalue2n	setsvalue\n"
    "#define setobj2t	setobj\n"
    "typedef TValue *StkId;\n"
    "typedef struct TString {\n"
    "	CommonHeader;\n"
    "	lu_byte extra;\n"
    "	lu_byte shrlen;\n"
    "	unsigned int hash;\n"
    "	union {\n"
    "		size_t lnglen;\n"
    "		struct TString *hnext;\n"
    "	} u;\n"
    "} TString;\n"
    "typedef union UTString {\n"
    "	L_Umaxalign dummy;\n"
    "	TString tsv;\n"
    "} UTString;\n"
    "#define getstr(ts)  \\\n"
    "  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))\n"
    "#define svalue(o)       getstr(tsvalue(o))\n"
    "#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)\n"
    "#define vslen(o)	tsslen(tsvalue(o))\n"
    "typedef struct Udata {\n"
    "	CommonHeader;\n"
    "	LuaType ttuv_;\n"
    "	struct Table *metatable;\n"
    "	size_t len;\n"
    "	union Value user_;\n"
    "} Udata;\n"
    "typedef union UUdata {\n"
    "	L_Umaxalign dummy;\n"
    "	Udata uv;\n"
    "} UUdata;\n"
    "#define getudatamem(u)  \\\n"
    "  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))\n"
    "#define setuservalue(L,u,o) \\\n"
    "	{ const TValue *io=(o); Udata *iu = (u); \\\n"
    "	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \\\n"
    "	  checkliveness(L,io); }\n"
    "#define getuservalue(L,u,o) \\\n"
    "	{ TValue *io=(o); const Udata *iu = (u); \\\n"
    "	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \\\n"
    "	  checkliveness(L,io); }\n"
    "typedef enum {\n"
    "RAVI_TI_NIL,\n"
    "RAVI_TI_FALSE,\n"
    "RAVI_TI_TRUE,\n"
    "RAVI_TI_INTEGER,\n"
    "RAVI_TI_FLOAT,\n"
    "RAVI_TI_INTEGER_ARRAY,\n"
    "RAVI_TI_FLOAT_ARRAY,\n"
    "RAVI_TI_TABLE,\n"
    "RAVI_TI_STRING,\n"
    "RAVI_TI_FUNCTION,\n"
    "RAVI_TI_USERDATA,\n"
    "RAVI_TI_OTHER\n"
    "} ravi_type_index;\n"
    "typedef uint32_t ravi_type_map;\n"
    "#define RAVI_TM_NIL (((ravi_type_map)1)<<RAVI_TI_NIL)\n"
    "#define RAVI_TM_FALSE (((ravi_type_map)1)<<RAVI_TI_FALSE)\n"
    "#define RAVI_TM_TRUE (((ravi_type_map)1)<<RAVI_TI_TRUE)\n"
    "#define RAVI_TM_INTEGER (((ravi_type_map)1)<<RAVI_TI_INTEGER)\n"
    "#define RAVI_TM_FLOAT (((ravi_type_map)1)<<RAVI_TI_FLOAT)\n"
    "#define RAVI_TM_INTEGER_ARRAY (((ravi_type_map)1)<<RAVI_TI_INTEGER_ARRAY)\n"
    "#define RAVI_TM_FLOAT_ARRAY (((ravi_type_map)1)<<RAVI_TI_FLOAT_ARRAY)\n"
    "#define RAVI_TM_TABLE (((ravi_type_map)1)<<RAVI_TI_TABLE)\n"
    "#define RAVI_TM_STRING (((ravi_type_map)1)<<RAVI_TI_STRING)\n"
    "#define RAVI_TM_FUNCTION (((ravi_type_map)1)<<RAVI_TI_FUNCTION)\n"
    "#define RAVI_TM_USERDATA (((ravi_type_map)1)<<RAVI_TI_USERDATA)\n"
    "#define RAVI_TM_OTHER (((ravi_type_map)1)<<RAVI_TI_OTHER)\n"
    "#define RAVI_TM_FALSISH (RAVI_TM_NIL | RAVI_TM_FALSE)\n"
    "#define RAVI_TM_TRUISH (~RAVI_TM_FALSISH)\n"
    "#define RAVI_TM_BOOLEAN (RAVI_TM_FALSE | RAVI_TM_TRUE)\n"
    "#define RAVI_TM_NUMBER (RAVI_TM_INTEGER | RAVI_TM_FLOAT)\n"
    "#define RAVI_TM_INDEXABLE (RAVI_TM_INTEGER_ARRAY | RAVI_TM_FLOAT_ARRAY | RAVI_TM_TABLE)\n"
    "#define RAVI_TM_STRING_OR_NIL (RAVI_TM_STRING | RAVI_TM_NIL)\n"
    "#define RAVI_TM_FUNCTION_OR_NIL (RAVI_TM_FUNCTION | RAVI_TM_NIL)\n"
    "#define RAVI_TM_BOOLEAN_OR_NIL (RAVI_TM_BOOLEAN | RAVI_TM_NIL)\n"
    "#define RAVI_TM_USERDATA_OR_NIL (RAVI_TM_USERDATA | RAVI_TM_NIL)\n"
    "#define RAVI_TM_ANY (~0)\n"
    "typedef enum {\n"
    "RAVI_TNIL = RAVI_TM_NIL,           /* NIL */\n"
    "RAVI_TNUMINT = RAVI_TM_INTEGER,    /* integer number */\n"
    "RAVI_TNUMFLT = RAVI_TM_FLOAT,        /* floating point number */\n"
    "RAVI_TNUMBER = RAVI_TM_NUMBER,\n"
    "RAVI_TARRAYINT = RAVI_TM_INTEGER_ARRAY,      /* array of ints */\n"
    "RAVI_TARRAYFLT = RAVI_TM_FLOAT_ARRAY,      /* array of doubles */\n"
    "RAVI_TTABLE = RAVI_TM_TABLE,         /* Lua table */\n"
    "RAVI_TSTRING = RAVI_TM_STRING_OR_NIL,        /* string */\n"
    "RAVI_TFUNCTION = RAVI_TM_FUNCTION_OR_NIL,      /* Lua or C Function */\n"
    "RAVI_TBOOLEAN = RAVI_TM_BOOLEAN_OR_NIL,       /* boolean */\n"
    "RAVI_TTRUE = RAVI_TM_TRUE,\n"
    "RAVI_TFALSE = RAVI_TM_FALSE,\n"
    "RAVI_TUSERDATA = RAVI_TM_USERDATA_OR_NIL,      /* userdata or lightuserdata */\n"
    "RAVI_TANY = RAVI_TM_ANY,      /* Lua dynamic type */\n"
    "} ravitype_t;\n"
    "typedef struct Upvaldesc {\n"
    "	TString *name;\n"
    "	TString *usertype;\n"
    "	ravi_type_map ravi_type;\n"
    "	lu_byte instack;\n"
    "	lu_byte idx;\n"
    "} Upvaldesc;\n"
    "typedef struct LocVar {\n"
    "	TString *varname;\n"
    "	TString *usertype;\n"
    "	int startpc;\n"
    "	int endpc;\n"
    "	ravi_type_map ravi_type;\n"
    "} LocVar;\n"
    "typedef enum {\n"
    "	RAVI_JIT_NOT_COMPILED = 0,\n"
    "	RAVI_JIT_CANT_COMPILE = 1,\n"
    "	RAVI_JIT_COMPILED = 2\n"
    "} ravi_jit_status_t;\n"
    "typedef enum {\n"
    "	RAVI_JIT_FLAG_NONE = 0,\n"
    "	RAVI_JIT_FLAG_HASFORLOOP = 1\n"
    "} ravi_jit_flag_t;\n"
    "typedef struct RaviJITProto {\n"
    "	lu_byte jit_status;\n"
    "	lu_byte jit_flags;\n"
    "	unsigned short execution_count;\n"
    "	void *jit_data;\n"
    "	lua_CFunction jit_function;\n"
    "} RaviJITProto;\n"
    "typedef struct Proto {\n"
    "	CommonHeader;\n"
    "	lu_byte numparams;\n"
    "	lu_byte is_vararg;\n"
    "	lu_byte maxstacksize;\n"
    "	int sizeupvalues;\n"
    "	int sizek;\n"
    "	int sizecode;\n"
    "	int sizelineinfo;\n"
    "	int sizep;\n"
    "	int sizelocvars;\n"
    "	int linedefined;\n"
    "	int lastlinedefined;\n"
    "	TValue *k;\n"
    "	Instruction *code;\n"
    "	struct Proto **p;\n"
    "	int *lineinfo;\n"
    "	LocVar *locvars;\n"
    "	Upvaldesc *upvalues;\n"
    "	struct LClosure *cache;\n"
    "	TString  *source;\n"
    "	GCObject *gclist;\n"
    "	RaviJITProto ravi_jit;\n"
    "} Proto;\n"
    "typedef struct UpVal UpVal;\n"
    "#define ClosureHeader \\\n"
    "	CommonHeader; lu_byte nupvalues; GCObject *gclist\n"
    "typedef struct CClosure {\n"
    "	ClosureHeader;\n"
    "	lua_CFunction f;\n"
    "	TValue upvalue[1];\n"
    "} CClosure;\n"
    "typedef struct LClosure {\n"
    "	ClosureHeader;\n"
    "	struct Proto *p;\n"
    "	UpVal *upvals[1];\n"
    "} LClosure;\n"
    "typedef union Closure {\n"
    "	CClosure c;\n"
    "	LClosure l;\n"
    "} Closure;\n"
    "#define isLfunction(o)	ttisLclosure(o)\n"
    "#define getproto(o)	(clLvalue(o)->p)\n"
    "typedef union TKey {\n"
    "	struct {\n"
    "		TValuefields;\n"
    "		int next;\n"
    "	} nk;\n"
    "	TValue tvk;\n"
    "} TKey;\n"
    "#define setnodekey(L,key,obj) \\\n"
    "	{ TKey *k_=(key); const TValue *io_=(obj); \\\n"
    "	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \\\n"
    "	  (void)L; checkliveness(L,io_); }\n"
    "typedef struct Node {\n"
    "	TValue i_val;\n"
    "	TKey i_key;\n"
    "} Node;\n"
    "typedef enum RaviArrayModifer {\n"
    " RAVI_ARRAY_SLICE = 1,\n"
    " RAVI_ARRAY_FIXEDSIZE = 2,\n"
    " RAVI_ARRAY_ALLOCATED = 4,\n"
    " RAVI_ARRAY_ISFLOAT = 8\n"
    "} RaviArrayModifier;\n"
    "enum {\n"
    " RAVI_ARRAY_MAX_INLINE = 3,\n"
    "};\n"
    "typedef struct RaviArray {\n"
    " CommonHeader;\n"
    " lu_byte flags;\n"
    " unsigned int len;\n"
    " unsigned int size;\n"
    " union {\n"
    "  lua_Number numarray[RAVI_ARRAY_MAX_INLINE];\n"
    "  lua_Integer intarray[RAVI_ARRAY_MAX_INLINE];\n"
    "  struct RaviArray* parent;\n"
    " };\n"
    " char *data;\n"
    " struct Table *metatable;\n"
    "} RaviArray;\n"
    "typedef struct Table {\n"
    " CommonHeader;\n"
    " lu_byte flags;\n"
    " lu_byte lsizenode;\n"
    " unsigned int sizearray;\n"
    " TValue *array;\n"
    " Node *node;\n"
    " Node *lastfree;\n"
    " struct Table *metatable;\n"
    " GCObject *gclist;\n"
    " unsigned int hmask;\n"
    "} Table;\n"
    "typedef struct Mbuffer {\n"
    "	char *buffer;\n"
    "	size_t n;\n"
    "	size_t buffsize;\n"
    "} Mbuffer;\n"
    "typedef struct stringtable {\n"
    "	TString **hash;\n"
    "	int nuse;\n"
    "	int size;\n"
    "} stringtable;\n"
    "struct lua_Debug;\n"
    "typedef intptr_t lua_KContext;\n"
    "typedef int(*lua_KFunction)(struct lua_State *L, int status, lua_KContext ctx);\n"
    "typedef void *(*lua_Alloc)(void *ud, void *ptr, size_t osize,\n"
    "	size_t nsize);\n"
    "typedef void(*lua_Hook)(struct lua_State *L, struct lua_Debug *ar);\n"
    "typedef struct CallInfo {\n"
    "	StkId func;\n"
    "	StkId	top;\n"
    "	struct CallInfo *previous, *next;\n"
    "	union {\n"
    "		struct {\n"
    "			StkId base;\n"
    "			const Instruction *savedpc;\n"
    "		} l;\n"
    "		struct {\n"
    "			lua_KFunction k;\n"
    "			ptrdiff_t old_errfunc;\n"
    "			lua_KContext ctx;\n"
    "		} c;\n"
    "	} u;\n"
    "	ptrdiff_t extra;\n"
    "	short nresults;\n"
    "	unsigned short callstatus;\n"
    "	unsigned short stacklevel;\n"
    "	lu_byte jitstatus;\n"
    "   lu_byte magic;\n"
    "} CallInfo;\n"
    "#define CIST_OAH	(1<<0)\n"
    "#define CIST_LUA	(1<<1)\n"
    "#define CIST_HOOKED	(1<<2)\n"
    "#define CIST_FRESH	(1<<3)\n"
    "#define CIST_YPCALL	(1<<4)\n"
    "#define CIST_TAIL	(1<<5)\n"
    "#define CIST_HOOKYIELD	(1<<6)\n"
    "#define CIST_LEQ	(1<<7)\n"
    "#define CIST_FIN	(1<<8)\n"
    "#define isLua(ci)	((ci)->callstatus & CIST_LUA)\n"
    "#define isJITed(ci) ((ci)->jitstatus)\n"
    "#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))\n"
    "#define getoah(st)	((st) & CIST_OAH)\n"
    "typedef struct global_State global_State;\n"
    "struct lua_State {\n"
    "	CommonHeader;\n"
    "	lu_byte status;\n"
    "	StkId top;\n"
    "	global_State *l_G;\n"
    "	CallInfo *ci;\n"
    "	const Instruction *oldpc;\n"
    "	StkId stack_last;\n"
    "	StkId stack;\n"
    "	UpVal *openupval;\n"
    "	GCObject *gclist;\n"
    "	struct lua_State *twups;\n"
    "	struct lua_longjmp *errorJmp;\n"
    "	CallInfo base_ci;\n"
    "	volatile lua_Hook hook;\n"
    "	ptrdiff_t errfunc;\n"
    "	int stacksize;\n"
    "	int basehookcount;\n"
    "	int hookcount;\n"
    "	unsigned short nny;\n"
    "	unsigned short nCcalls;\n"
    "	lu_byte hookmask;\n"
    "	lu_byte allowhook;\n"
    "	unsigned short nci;\n"
    "   lu_byte magic;\n"
    "};\n"
    "#define G(L)	(L->l_G)\n"
    "union GCUnion {\n"
    "	GCObject gc;\n"
    "	struct TString ts;\n"
    "	struct Udata u;\n"
    "	union Closure cl;\n"
    "	struct Table h;\n"
    "   struct RaviArray arr;\n"
    "	struct Proto p;\n"
    "	struct lua_State th;\n"
    "};\n"
    "struct UpVal {\n"
    "	TValue *v;\n"
#ifdef RAVI_DEFER_STATEMENT
    "       unsigned int refcount;\n"
    "       unsigned int flags;\n"
#else
    "	lu_mem refcount;\n"
#endif
    "	union {\n"
    "		struct {\n"
    "			UpVal *next;\n"
    "			int touched;\n"
    "		} open;\n"
    "		TValue value;\n"
    "	} u;\n"
    "};\n"
    "#define cast_u(o)	cast(union GCUnion *, (o))\n"
    "#define gco2ts(o)  \\\n"
    "	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))\n"
    "#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))\n"
    "#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))\n"
    "#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))\n"
    "#define gco2cl(o)  \\\n"
    "	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))\n"
    "#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))\n"
    "#define gco2array(o)  check_exp(((o)->tt == RAVI_TIARRAY || (o)->tt == RAVI_TFARRAY), &((cast_u(o))->arr))\n"
    "#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))\n"
    "#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))\n"
    "#define obj2gco(v) \\\n"
    "	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))\n"
    "#define LUA_FLOORN2I		0\n"
    "#define tonumber(o,n) \\\n"
    "  (ttisfloat(o) ? (*(n) = fltvalue(o), 1) : luaV_tonumber_(o,n))\n"
    "#define tointeger(o,i) \\\n"
    "  (ttisinteger(o) ? (*(i) = ivalue(o), 1) : luaV_tointeger(o,i,LUA_FLOORN2I))\n"
    "extern int luaV_tonumber_(const TValue *obj, lua_Number *n);\n"
    "extern int luaV_tointeger(const TValue *obj, lua_Integer *p, int mode);\n"
#ifdef RAVI_DEFER_STATEMENT
    "extern int luaF_close (lua_State *L, StkId level, int status);\n"
#else
    "extern void luaF_close (lua_State *L, StkId level);\n"
#endif
    "extern int luaD_poscall (lua_State *L, CallInfo *ci, StkId firstResult, int nres);\n"
    "extern void luaD_growstack (lua_State *L, int n);\n"
    "extern int luaV_equalobj(lua_State *L, const TValue *t1, const TValue *t2);\n"
    "extern int luaV_lessthan(lua_State *L, const TValue *l, const TValue *r);\n"
    "extern int luaV_lessequal(lua_State *L, const TValue *l, const TValue *r);\n"
    "extern void luaV_gettable (lua_State *L, const TValue *t, TValue *key, StkId val);\n"
    "extern void luaV_settable (lua_State *L, const TValue *t, TValue *key, StkId val);\n"
    "extern int luaV_execute(lua_State *L);\n"
    "extern int luaD_precall (lua_State *L, StkId func, int nresults, int op_call);\n"
    "extern void raviV_op_newtable(lua_State *L, CallInfo *ci, TValue *ra, int b, int c);\n"
    "extern void raviV_op_newarrayint(lua_State *L, CallInfo *ci, TValue *ra);\n"
    "extern void raviV_op_newarrayfloat(lua_State *L, CallInfo *ci, TValue *ra);\n"
    "extern void luaO_arith (lua_State *L, int op, const TValue *p1, const TValue *p2, TValue *res);\n"
    "extern void raviV_op_setlist(lua_State *L, CallInfo *ci, TValue *ra, int b, int c);\n"
    "extern void raviV_op_concat(lua_State *L, CallInfo *ci, int a, int b, int c);\n"
    "extern void raviV_op_closure(lua_State *L, CallInfo *ci, LClosure *cl, int a, int Bx);\n"
    "extern void raviV_op_vararg(lua_State *L, CallInfo *ci, LClosure *cl, int a, int b);\n"
    "extern void luaV_objlen (lua_State *L, StkId ra, const TValue *rb);\n"
    "extern int luaV_forlimit(const TValue *obj, lua_Integer *p, lua_Integer step, int *stopnow);\n"
    "extern void raviV_op_setupval(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_op_setupvali(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_op_setupvalf(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_op_setupvalai(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_op_setupvalaf(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_op_setupvalt(lua_State *L, LClosure *cl, TValue *ra, int b);\n"
    "extern void raviV_raise_error(lua_State *L, int errorcode);\n"
    "extern void raviV_raise_error_with_info(lua_State *L, int errorcode, const char *info);\n"
    "extern void luaD_call (lua_State *L, StkId func, int nResults);\n"
    "extern void raviH_set_int(lua_State *L, RaviArray *t, lua_Unsigned key, lua_Integer value);\n"
    "extern void raviH_set_float(lua_State *L, RaviArray *t, lua_Unsigned key, lua_Number value);\n"
    "extern int raviV_check_usertype(lua_State *L, TString *name, const TValue *o);\n"
    "extern void luaT_trybinTM (lua_State *L, const TValue *p1, const TValue *p2, TValue *res, TMS event);\n"
    "extern void raviV_gettable_sskey(lua_State *L, const TValue *t, TValue *key, TValue *val);\n"
    "extern void raviV_settable_sskey(lua_State *L, const TValue *t, TValue *key, TValue *val);\n"
    "extern void raviV_gettable_i(lua_State *L, const TValue *t, TValue *key, TValue *val);\n"
    "extern void raviV_settable_i(lua_State *L, const TValue *t, TValue *key, TValue *val);\n"
#ifdef RAVI_DEFER_STATEMENT
    "extern void raviV_op_defer(lua_State *L, TValue *ra);\n"
#endif
    "extern lua_Integer luaV_shiftl(lua_Integer x, lua_Integer y);\n"
    "extern void ravi_dump_value(lua_State *L, const struct lua_TValue *v);\n"
    "extern void raviV_op_bnot(lua_State *L, TValue *ra, TValue *rb);\n"
    "extern void *luaM_realloc_ (lua_State *L, void *block, size_t osize, size_t nsize);\n"
    "extern LClosure *luaF_newLclosure (lua_State *L, int n);\n"
    "extern TString *luaS_newlstr (lua_State *L, const char *str, size_t l);\n"
    "extern Proto *luaF_newproto (lua_State *L);\n"
    "extern void luaD_inctop (lua_State *L);\n"
    "#define luaM_reallocv(L,b,on,n,e) luaM_realloc_(L, (b), (on)*(e), (n)*(e))\n"
    "#define luaM_newvector(L,n,t) cast(t *, luaM_reallocv(L, NULL, 0, n, sizeof(t)))\n"
    "#define R(i) (base + i)\n"
    "#define K(i) (k + i)\n"
    "#define S(i) (stackbase + i)\n"
    "#define stackoverflow(L, n) (((int)(L->top - L->stack) + (n) + 5) >= L->stacksize)\n"
    "#define savestack(L,p)		((char *)(p) - (char *)L->stack)\n"
    "#define restorestack(L,n)	((TValue *)((char *)L->stack + (n)))\n"
    "#define tonumberns(o,n) \\\n"
    "	(ttisfloat(o) ? ((n) = fltvalue(o), 1) : \\\n"
    "	(ttisinteger(o) ? ((n) = cast_num(ivalue(o)), 1) : 0))\n"
    "#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))\n"
    "#define nan (0./0.)\n"
    "#define inf (1./0.)\n"
    "#define luai_numunm(L,a)        (-(a))\n";

struct function {
	Proc *proc;
	TextBuffer prologue;
	TextBuffer body;
	struct Ravi_CompilerInterface *api;
};

/* readonly statics */
static const char *int_var_prefix = "i_";
static const char *flt_var_prefix = "f_";
// static Pseudo NIL_pseudo = {.type = PSEUDO_NIL};

enum errorcode {
	Error_integer_expected,
	Error_number_expected,
	Error_integer_array_expected,
	Error_number_array_expected,
	Error_table_expected,
	Error_upval_needs_integer,
	Error_upval_needs_number,
	Error_upval_needs_integer_array,
	Error_upval_needs_number_array,
	Error_upval_needs_table,
	Error_for_limit_must_be_number,
	Error_for_step_must_be_number,
	Error_for_initial_value_must_be_number,
	Error_array_out_of_bounds,
	Error_string_expected,
	Error_closure_expected,
	Error_type_mismatch,
};

// Opcodes used by luaO_arith
enum {
	LUA_OPADD = 0, /* ORDER TM, ORDER OP */
	LUA_OPSUB = 1,
	LUA_OPMUL = 2,
	LUA_OPMOD = 3,
	LUA_OPPOW = 4,
	LUA_OPDIV = 5,
	LUA_OPIDIV = 6,
	LUA_OPBAND = 7,
	LUA_OPBOR = 8,
	LUA_OPBXOR = 9,
	LUA_OPSHL = 10,
	LUA_OPSHR = 11,
	LUA_OPUNM = 12,
	LUA_OPBNOT = 13
};

static inline Pseudo *get_operand(Instruction *insn, unsigned idx)
{
	return (Pseudo *)raviX_ptrlist_nth_entry((PtrList *)insn->operands, idx);
}

static inline Pseudo *get_first_operand(Instruction *insn)
{
	return (Pseudo *)raviX_ptrlist_first((PtrList *)insn->operands);
}

static inline Pseudo *get_last_operand(Instruction *insn)
{
	return (Pseudo *)raviX_ptrlist_last((PtrList *)insn->operands);
}

static inline Pseudo *get_target(Instruction *insn, unsigned idx)
{
	return (Pseudo *)raviX_ptrlist_nth_entry((PtrList *)insn->targets, idx);
}

static inline Pseudo *get_first_target(Instruction *insn)
{
	return (Pseudo *)raviX_ptrlist_first((PtrList *)insn->targets);
}

static inline Pseudo *get_last_target(Instruction *insn)
{
	return (Pseudo *)raviX_ptrlist_last((PtrList *)insn->targets);
}

static inline unsigned get_num_operands(Instruction *insn)
{
	return raviX_ptrlist_size((const PtrList *)insn->operands);
}

static inline unsigned get_num_targets(Instruction *insn) { return raviX_ptrlist_size((const PtrList *)insn->targets); }

static inline unsigned get_num_instructions(BasicBlock *bb) { return raviX_ptrlist_size((const PtrList *)bb->insns); }

static inline unsigned get_num_childprocs(Proc *proc) { return raviX_ptrlist_size((const PtrList *)proc->procs); }

/**
 * Helper to generate a list of primitive C variables representing temp int/float values.
 */
static void emit_vars(const char *type, const char *prefix, PseudoGenerator *gen, TextBuffer *mb)
{
	if (gen->next_reg == 0)
		return;
	for (unsigned i = 0; i < gen->next_reg; i++) {
		if (i == 0) {
			raviX_buffer_add_fstring(mb, "%s ", type);
		}
		if (i > 0) {
			raviX_buffer_add_string(mb, " = 0, ");
		}
		raviX_buffer_add_fstring(mb, "%s%d", prefix, i);
	}
	raviX_buffer_add_string(mb, " = 0;\n");
}

static void emit_varname(const Pseudo *pseudo, TextBuffer *mb)
{
	if (pseudo->type == PSEUDO_TEMP_INT || pseudo->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_fstring(mb, "%s%d", int_var_prefix, pseudo->regnum);
	} else if (pseudo->type == PSEUDO_TEMP_FLT) {
		raviX_buffer_add_fstring(mb, "%s%d", flt_var_prefix, pseudo->regnum);
	} else {
		fprintf(stderr, "Unexpected pseudo type %d\n", pseudo->type);
		assert(0);
	}
}

static void initfn(struct function *fn, Proc *proc, struct Ravi_CompilerInterface *api)
{
	fn->proc = proc;
	fn->api = api;
	/* Set a name that can be used later to retrieve the compiled code */
	snprintf(proc->funcname, sizeof proc->funcname, "__ravifunc_%d", proc->id);
	raviX_buffer_init(&fn->prologue, 4096);
	raviX_buffer_init(&fn->body, 4096);
	raviX_buffer_add_fstring(&fn->prologue, "static int %s(lua_State *L) {\n", proc->funcname);
	raviX_buffer_add_string(&fn->prologue, "int error_code = 0;\n");
	raviX_buffer_add_string(&fn->prologue, "int result = 0;\n");
	raviX_buffer_add_string(&fn->prologue, "CallInfo *ci = L->ci;\n");
	raviX_buffer_add_string(&fn->prologue, "LClosure *cl = clLvalue(ci->func);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue *k = cl->p->k;\n");
	raviX_buffer_add_string(&fn->prologue, "StkId base = ci->u.l.base;\n");
	emit_vars("lua_Integer", int_var_prefix, &proc->temp_int_pseudos, &fn->prologue);
	emit_vars("lua_Number", flt_var_prefix, &proc->temp_flt_pseudos, &fn->prologue);
	// Following are temp dummy regs
	// In ops like luaV_settable we may use up to two variables
	raviX_buffer_add_string(&fn->prologue, "TValue ival0; settt_(&ival0, LUA_TNUMINT);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue fval0; settt_(&fval0, LUA_TNUMFLT);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue bval0; settt_(&bval0, LUA_TBOOLEAN);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue ival1; settt_(&ival1, LUA_TNUMINT);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue fval1; settt_(&fval1, LUA_TNUMFLT);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue bval1; settt_(&bval1, LUA_TBOOLEAN);\n");
	raviX_buffer_add_string(&fn->prologue, "TValue nilval; setnilvalue(&nilval);\n");
}

static void cleanup(struct function *fn)
{
	raviX_buffer_free(&fn->prologue);
	raviX_buffer_free(&fn->body);
}

static void emit_reload_base(struct function *fn) { raviX_buffer_add_string(&fn->body, "base = ci->u.l.base;\n"); }

static inline unsigned num_locals(Proc *proc) { return proc->local_pseudos.next_reg; }

static inline unsigned num_temps(Proc *proc) { return proc->temp_pseudos.next_reg; }

/*
 * Max stack size is number of Lua vars and any temps that live on Lua stack during execution.
 * Note that this is the number of slots that is known to the compiler - at runtime additional
 * stack space may be needed when making function calls - that is not accounted for here.
 */
static unsigned compute_max_stack_size(Proc *proc) { return num_locals(proc) + num_temps(proc); }

/**
 * Computes the register offset from base. Input pseudo must be a local variable,
 * or temp register or range register (on Lua stack)
 */
static unsigned compute_register_from_base(struct function *fn, const Pseudo *pseudo)
{
	switch (pseudo->type) {
	case PSEUDO_TEMP_ANY:
	case PSEUDO_RANGE: // Compute starting register
	case PSEUDO_RANGE_SELECT:
		// All temps start after the locals
		return pseudo->regnum + num_locals(fn->proc);
	case PSEUDO_SYMBOL:
		if (pseudo->symbol->symbol_type == SYM_LOCAL) {
			return pseudo->regnum;
		}
		// fallthrough
	default:
		assert(false);
		return (unsigned)-1;
	}
}

// Check if two pseudos point to the same register
// note we cannot easily check PSEUDO_LUASTACK type because there may
// be var args between CI->func and base. So stackbase may not be base-1 always.
static bool refers_to_same_register(struct function *fn, Pseudo *src, Pseudo *dst)
{
	static bool reg_pseudos[] = {
	    /* [PSEUDO_SYMBOL] =*/true,	    /* An object of type lua_symbol representing local var or upvalue */
	    /* [PSEUDO_TEMP_FLT] =*/false,  /* A floating point temp - may also be used for locals that don't escape */
	    /* [PSEUDO_TEMP_INT] =*/false,  /* An integer temp - may also be used for locals that don't escape */
	    /* [PSEUDO_TEMP_BOOL] =*/false, /* An (bool) integer temp - may also be used for locals that don't escape */
	    /* [PSEUDO_TEMP_ANY] =*/true,   /* A temp of any type - will always be on Lua stack */
	    /* [PSEUDO_CONSTANT] =*/false,  /* A literal value */
	    /* [PSEUDO_PROC] =*/false,	    /* A proc / function */
	    /* [PSEUDO_NIL] =*/false,
	    /* [PSEUDO_TRUE] =*/false,
	    /* [PSEUDO_FALSE] =*/false,
	    /* [PSEUDO_BLOCK] =*/false,	      /* Points to a basic block, used as targets for jumps */
	    /* [PSEUDO_RANGE] =*/true,	      /* Represents a range of registers from a certain starting register */
	    /* [PSEUDO_RANGE_SELECT] =*/true, /* Picks a certain register from a range */
					      /* TODO we need a type for var args */
	    /* [PSEUDO_LUASTACK] =*/true /* Specifies a Lua stack position - not used by linearizer - for use by codegen
					  */
	};
	if (!reg_pseudos[src->type] || !reg_pseudos[dst->type])
		return false;
	if (src->type == PSEUDO_LUASTACK || dst->type == PSEUDO_LUASTACK) {
		return src->type == dst->type && src->stackidx == dst->stackidx;
	}
	if (src->type == PSEUDO_SYMBOL && dst->type != PSEUDO_SYMBOL)
		// a temp reg can never equate local reg
		return false;
	if (src->type == PSEUDO_SYMBOL && dst->type == PSEUDO_SYMBOL) {
		// up-values are not registers
		if (src->symbol->symbol_type != SYM_LOCAL || dst->symbol->symbol_type != SYM_LOCAL) {
			return false;
		}
	}
	return compute_register_from_base(fn, src) == compute_register_from_base(fn, dst);
}

/*
Outputs accessor for a pseudo so that the accessor is always of type
TValue *. Thus for constants, we need to use a temp stack variable of type TValue.
The issue is what happens if we need two values at the same time and both are constants
of the same type. This is where the discriminator comes in - to help differentiate.
*/
static int emit_reg_accessor(struct function *fn, const Pseudo *pseudo, unsigned discriminator)
{
	if (pseudo->type == PSEUDO_LUASTACK) {
		// Note pseudo->stackidx is relative to ci->func
		// But ci->func is not always base-1 because of var args
		// Therefore we need a different way to compute these
		raviX_buffer_add_fstring(&fn->body, "S(%d)", pseudo->stackidx);
	} else if (pseudo->type == PSEUDO_TEMP_ANY || pseudo->type == PSEUDO_RANGE ||
		   pseudo->type == PSEUDO_RANGE_SELECT) {
		// we put all temps on Lua stack after the locals
		raviX_buffer_add_fstring(&fn->body, "R(%d)", compute_register_from_base(fn, pseudo));
	} else if (pseudo->type == PSEUDO_SYMBOL) {
		if (pseudo->symbol->symbol_type == SYM_LOCAL) {
			raviX_buffer_add_fstring(&fn->body, "R(%d)", pseudo->regnum);
		} else if (pseudo->symbol->symbol_type == SYM_UPVALUE) {
			raviX_buffer_add_fstring(&fn->body, "cl->upvals[%d]->v", pseudo->regnum);
		} else {
			fn->api->error_message(fn->api->context, "Unexpected pseudo symbol type");
			assert(0);
			return -1;
		}
	} else if (pseudo->type == PSEUDO_CONSTANT) {
		if (pseudo->constant->type == RAVI_TSTRING) {
			unsigned k = pseudo->constant->index;
			raviX_buffer_add_fstring(&fn->body, "K(%d)", k);
		} else if (pseudo->constant->type == RAVI_TNUMINT) {
			raviX_buffer_add_fstring(&fn->body, "&ival%u; ival%u.value_.i = %lld", discriminator,
						 discriminator, pseudo->constant->i);
		} else if (pseudo->constant->type == RAVI_TNUMFLT) {
			raviX_buffer_add_fstring(&fn->body, "&fval%u; fval%u.value_.n = %g", discriminator,
						 discriminator, pseudo->constant->n);
		} else if (pseudo->constant->type == RAVI_TNIL) {
			raviX_buffer_add_string(&fn->body, "&nilval");
		} else if (pseudo->constant->type == RAVI_TBOOLEAN) {
			raviX_buffer_add_fstring(&fn->body, "&bval%u; bval%u.value_.b = %d", discriminator,
						 discriminator, (int)pseudo->constant->i);
		} else {
			fn->api->error_message(fn->api->context, "Unexpected pseudo constant type");
			assert(0);
			return -1;
		}
	} else if (pseudo->type == PSEUDO_TEMP_FLT) {
		raviX_buffer_add_fstring(&fn->body, "&fval%u; fval%u.value_.n = ", discriminator, discriminator);
		emit_varname(pseudo, &fn->body);
	} else if (pseudo->type == PSEUDO_TEMP_INT) {
		raviX_buffer_add_fstring(&fn->body, "&ival%u; ival%u.value_.i = ", discriminator, discriminator);
		emit_varname(pseudo, &fn->body);
	} else if (pseudo->type == PSEUDO_NIL) {
		raviX_buffer_add_string(&fn->body, "&nilval");
	} else if (pseudo->type == PSEUDO_TRUE) {
		raviX_buffer_add_fstring(&fn->body, "&bval%u; bval%u.value_.b = 1", discriminator, discriminator);
	} else if (pseudo->type == PSEUDO_FALSE) {
		raviX_buffer_add_fstring(&fn->body, "&bval%u; bval%u.value_.b = 0", discriminator, discriminator);
	} else if (pseudo->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_fstring(&fn->body, "&bval%u; bval%u.value_.b = ", discriminator, discriminator);
		emit_varname(pseudo, &fn->body);
	} else {
		fn->api->error_message(fn->api->context, "Unexpected pseudo type");
		assert(0);
		return -1;
	}
	return 0;
}

/*copy floating point value to a temporary float */
static int emit_move_flttemp(struct function *fn, Pseudo *src, Pseudo *dst)
{
	if (src->type == PSEUDO_CONSTANT) {
		if (src->constant->type == RAVI_TNUMFLT) {
			emit_varname(dst, &fn->body);
			raviX_buffer_add_fstring(&fn->body, " = %.16g;\n", src->constant->n);
		} else if (src->constant->type == RAVI_TNUMINT) {
			emit_varname(dst, &fn->body);
			raviX_buffer_add_fstring(&fn->body, " = (lua_Number)%lld;\n", src->constant->i);
		} else {
			assert(0);
			return -1;
		}
	} else if (src->type == PSEUDO_TEMP_FLT) {
		emit_varname(dst, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
		emit_varname(src, &fn->body);
		raviX_buffer_add_string(&fn->body, ";\n");
	} else if (src->type == PSEUDO_LUASTACK || src->type == PSEUDO_TEMP_ANY || src->type == PSEUDO_SYMBOL) {
		raviX_buffer_add_string(&fn->body, "{\nTValue *reg = ");
		emit_reg_accessor(fn, src, 0);
		raviX_buffer_add_string(&fn->body, ";\n");
		emit_varname(dst, &fn->body);
		raviX_buffer_add_string(&fn->body, " = fltvalue(reg);\n}\n");
	} else {
		assert(0);
		return -1;
	}
	return 0;
}

/*copy integer value to temporary int */
static int emit_move_inttemp(struct function *fn, Pseudo *src, Pseudo *dst)
{
	if (src->type == PSEUDO_CONSTANT) {
		if (src->constant->type == RAVI_TNUMINT) {
			emit_varname(dst, &fn->body);
			raviX_buffer_add_fstring(&fn->body, " = %lld;\n", src->constant->i);
		} else {
			// FIXME can we have float value?
			assert(0);
			return -1;
		}
	} else if (src->type == PSEUDO_TEMP_INT || src->type == PSEUDO_TEMP_BOOL) {
		emit_varname(dst, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
		emit_varname(src, &fn->body);
		raviX_buffer_add_string(&fn->body, ";\n");
	} else if (src->type == PSEUDO_LUASTACK || src->type == PSEUDO_TEMP_ANY || src->type == PSEUDO_SYMBOL) {
		raviX_buffer_add_string(&fn->body, "{\nTValue *reg = ");
		emit_reg_accessor(fn, src, 0);
		raviX_buffer_add_string(&fn->body, ";\n");
		emit_varname(dst, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ivalue(reg);\n}\n");
	} else {
		assert(0);
		return -1;
	}
	return 0;
}

/* copy a value from source pseudo to destination pseudo.*/
static int emit_move(struct function *fn, Pseudo *src, Pseudo *dst)
{
	if (dst->type == PSEUDO_TEMP_FLT) {
		emit_move_flttemp(fn, src, dst);
	} else if (dst->type == PSEUDO_TEMP_INT || dst->type == PSEUDO_TEMP_BOOL) {
		emit_move_inttemp(fn, src, dst);
	} else if (dst->type == PSEUDO_TEMP_ANY || dst->type == PSEUDO_SYMBOL || dst->type == PSEUDO_LUASTACK) {
		if (src->type == PSEUDO_LUASTACK || src->type == PSEUDO_TEMP_ANY || src->type == PSEUDO_SYMBOL ||
		    src->type == PSEUDO_RANGE_SELECT) {
			// Only emit a move if we are not referencing the same register
			if (!refers_to_same_register(fn, src, dst)) {
				raviX_buffer_add_string(&fn->body, "{\nconst TValue *src_reg = ");
				emit_reg_accessor(fn, src, 0);
				raviX_buffer_add_string(&fn->body, ";\nTValue *dst_reg = ");
				emit_reg_accessor(fn, dst, 0);
				// FIXME - check value assignment approach
				raviX_buffer_add_string(
				    &fn->body,
				    ";\ndst_reg->tt_ = src_reg->tt_;\ndst_reg->value_.n = src_reg->value_.n;\n}\n");
			}
		} else if (src->type == PSEUDO_TEMP_INT) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_string(&fn->body, ";\nsetivalue(dst_reg, ");
			emit_varname(src, &fn->body);
			raviX_buffer_add_string(&fn->body, ");\n}\n");
		} else if (src->type == PSEUDO_TEMP_FLT) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_string(&fn->body, ";\nsetfltvalue(dst_reg, ");
			emit_varname(src, &fn->body);
			raviX_buffer_add_string(&fn->body, ");\n}\n");
		} else if (src->type == PSEUDO_TRUE || src->type == PSEUDO_FALSE) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_fstring(&fn->body, ";\nsetbvalue(dst_reg, %d);\n}\n",
						 src->type == PSEUDO_TRUE ? 1 : 0);
		} else if (src->type == PSEUDO_TEMP_BOOL) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_string(&fn->body, ";\nsetbvalue(dst_reg, ");
			emit_varname(src, &fn->body);
			raviX_buffer_add_string(&fn->body, ");\n}\n");
		} else if (src->type == PSEUDO_NIL) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_string(&fn->body, ";\nsetnilvalue(dst_reg);\n}\n");
		} else if (src->type == PSEUDO_CONSTANT) {
			raviX_buffer_add_string(&fn->body, "{\nTValue *dst_reg = ");
			emit_reg_accessor(fn, dst, 0);
			raviX_buffer_add_string(&fn->body, ";\n");
			if (src->constant->type == RAVI_TNUMINT) {
				raviX_buffer_add_fstring(&fn->body, "setivalue(dst_reg, %lld);\n", src->constant->i);
			} else if (src->constant->type == RAVI_TNUMFLT) {
				raviX_buffer_add_fstring(&fn->body, "setfltvalue(dst_reg, %g);\n", src->constant->n);
			} else if (src->constant->type == RAVI_TBOOLEAN) {
				raviX_buffer_add_fstring(&fn->body, "setbvalue(dst_reg, %i);\n", (int)src->constant->i);
			} else if (src->constant->type == RAVI_TNIL) {
				raviX_buffer_add_string(&fn->body, "setnilvalue(dst_reg);\n");
			} else if (src->constant->type == RAVI_TSTRING) {
				raviX_buffer_add_string(&fn->body, "TValue *src_reg = ");
				emit_reg_accessor(fn, src, 0);
				raviX_buffer_add_string(&fn->body, ";\n");
				raviX_buffer_add_string(
				    &fn->body,
				    "dst_reg->tt_ = src_reg->tt_; dst_reg->value_.gc = src_reg->value_.gc;\n");
			} else {
				assert(0);
				return -1;
			}
			raviX_buffer_add_string(&fn->body, "}\n");
		} else {
			/* range pseudos not supported yet */
			assert(0);
			return -1;
		}
	} else {
		assert(0);
		return -1;
	}
	return 0;
}

static int emit_jump(struct function *fn, Pseudo *pseudo)
{
	assert(pseudo->type == PSEUDO_BLOCK);
	raviX_buffer_add_fstring(&fn->body, "goto L%d;\n", pseudo->block->index);
	return 0;
}

static int emit_op_cbr(struct function *fn, Instruction *insn)
{
	assert(insn->opcode == op_cbr);
	Pseudo *cond_pseudo = get_operand(insn, 0);
	if (cond_pseudo->type == PSEUDO_FALSE || cond_pseudo->type == PSEUDO_NIL) {
		emit_jump(fn, get_target(insn, 1));
	} else if (cond_pseudo->type == PSEUDO_TRUE || cond_pseudo->type == PSEUDO_CONSTANT) {
		emit_jump(fn, get_target(insn, 0));
	} else if (cond_pseudo->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_string(&fn->body, "{");
		raviX_buffer_add_string(&fn->body, " if (");
		emit_varname(cond_pseudo, &fn->body);
		raviX_buffer_add_fstring(&fn->body, " != 0) goto L%d;", get_target(insn, 0)->block->index);
		raviX_buffer_add_fstring(&fn->body, " else goto L%d; ", get_target(insn, 1)->block->index);
		raviX_buffer_add_string(&fn->body, "}\n");
	} else if (cond_pseudo->type == PSEUDO_TEMP_ANY || cond_pseudo->type == PSEUDO_SYMBOL) {
		raviX_buffer_add_string(&fn->body, "{\nconst TValue *src_reg = ");
		emit_reg_accessor(fn, cond_pseudo, 0);
		raviX_buffer_add_fstring(&fn->body, ";\nif (!l_isfalse(src_reg)) goto L%d;\n",
					 get_target(insn, 0)->block->index);
		raviX_buffer_add_fstring(&fn->body, "else goto L%d;\n", get_target(insn, 1)->block->index);
		raviX_buffer_add_string(&fn->body, "}\n");
	} else {
		assert(0);
		return -1;
	}
	return 0;
}

static int emit_op_br(struct function *fn, Instruction *insn)
{
	assert(insn->opcode == op_br);
	return emit_jump(fn, get_target(insn, 0));
}

static int emit_op_mov(struct function *fn, Instruction *insn)
{
	assert(insn->opcode == op_mov || insn->opcode == op_movi || insn->opcode == op_movf);
	return emit_move(fn, get_operand(insn, 0), get_target(insn, 0));
}

static int emit_op_ret(struct function *fn, Instruction *insn)
{
	// TODO Only call luaF_close if needed (i.e. some variable escaped)
#ifdef RAVI_DEFER_STATEMENT
	if (raviX_ptrlist_size((const PtrList *)fn->proc->procs) > 0) {
		raviX_buffer_add_string(&fn->body, "{\nluaF_close(L, base, LUA_OK);\n");
		raviX_buffer_add_string(&fn->body, "base = ci->u.l.base;\n");
		raviX_buffer_add_string(&fn->body, "}\n");
	}
#else
	if (raviX_ptrlist_size((const PtrList *)fn->proc->procs) > 0) {
		raviX_buffer_add_string(&fn->body, "luaF_close(L, base);\n");
	}
#endif
	raviX_buffer_add_string(&fn->body, "{\n");
	/* Results are copied to stack position given by ci->func and above.
	 * stackbase is set here so S(n) refers to (stackbase+n)
	 */
	raviX_buffer_add_string(&fn->body, " TValue *stackbase = ci->func;\n");
	raviX_buffer_add_string(&fn->body, " int wanted = ci->nresults;\n");
	raviX_buffer_add_string(&fn->body, " result = wanted == -1 ? 0 : 1;\n"); /* see OP_RETURN impl in JIT */
	int n = get_num_operands(insn);
	if (n > 0) {
		Pseudo *last_operand = get_operand(insn, n - 1);
		/* the last operand might be a range pseudo */
		if (last_operand->type == PSEUDO_RANGE) {
			raviX_buffer_add_string(&fn->body, " if (wanted == -1) {\n");
			raviX_buffer_add_string(&fn->body, "  TValue *start_vararg = ");
			Pseudo tmp = {.type = PSEUDO_TEMP_ANY, .regnum = last_operand->regnum};
			emit_reg_accessor(fn, &tmp, 0);
			raviX_buffer_add_string(&fn->body, " ;\n");
			raviX_buffer_add_fstring(&fn->body, "  wanted = (L->top - start_vararg) + %d;\n", n - 1);
			raviX_buffer_add_string(&fn->body, " }\n");
		} else {
			raviX_buffer_add_fstring(&fn->body, " if (wanted == -1) wanted = %d;\n", n);
		}
	} else {
		raviX_buffer_add_string(&fn->body, " if (wanted == -1) wanted = 0;\n");
	}
	Pseudo *pseudo;
	int i = 0;
	raviX_buffer_add_string(&fn->body, " int j = 0;\n");
	FOR_EACH_PTR(insn->operands, Pseudo, pseudo)
	{
		if (pseudo->type != PSEUDO_RANGE) {
			Pseudo dummy_dest = {.type = PSEUDO_LUASTACK, .stackidx = i}; /* will go to stackbase[i] */
			raviX_buffer_add_fstring(&fn->body, " if (%d < wanted) {\n", i);
			/* FIXME last argument might be a range pseudo */
			emit_move(fn, pseudo, &dummy_dest);
			raviX_buffer_add_string(&fn->body, " }\n");
			raviX_buffer_add_fstring(&fn->body, " j++;\n");
			i++;
		} else {
			/* copy values starting at the range to L->top */
			// raviX_buffer_add_fstring(&fn->body, " j = %d;\n", i);
			raviX_buffer_add_fstring(&fn->body, " {\n int reg = %d;\n", pseudo->regnum);
			raviX_buffer_add_string(&fn->body, "  while (j < wanted) {\n");
			raviX_buffer_add_string(&fn->body, "   TValue *dest_reg = S(j);\n");
			raviX_buffer_add_string(&fn->body, "   TValue *src_reg = R(reg);\n");
			raviX_buffer_add_string(
			    &fn->body, "   dest_reg->tt_ = src_reg->tt_; dest_reg->value_.gc = src_reg->value_.gc;\n");
			raviX_buffer_add_string(&fn->body, "   j++, reg++;\n");
			raviX_buffer_add_string(&fn->body, "  }\n");
			raviX_buffer_add_string(&fn->body, " }\n");
		}
	}
	END_FOR_EACH_PTR(pseudo);
	/* Set any excess results to nil */
	raviX_buffer_add_string(&fn->body, " while (j < wanted) {\n");
	{
		raviX_buffer_add_string(&fn->body, "  setnilvalue(S(j));\n");
		raviX_buffer_add_string(&fn->body, "  j++;\n");
	}
	raviX_buffer_add_string(&fn->body, " }\n");
	/* FIXME the rule for L->top needs to be checked */
	raviX_buffer_add_string(&fn->body, " L->top = S(0) + wanted;\n");
	raviX_buffer_add_string(&fn->body, " L->ci = ci->previous;\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	emit_jump(fn, get_target(insn, 0));
	return 0;
}

/* Generate code for various types of load table operations */
static int emit_op_load_table(struct function *fn, Instruction *insn)
{
	const char *fname = "luaV_gettable";
	if (insn->opcode == op_tget_ikey) {
		fname = "raviV_gettable_i";
	} else if (insn->opcode == op_tget_skey) {
		fname = "raviV_gettable_sskey";
	}
	Pseudo *env = get_operand(insn, 0);
	Pseudo *varname = get_operand(insn, 1);
	Pseudo *dst = get_target(insn, 0);
	if (varname->type == PSEUDO_CONSTANT && varname->constant->type == RAVI_TSTRING) {
		if (varname->constant->s->len < 40) {
			fname = "raviV_gettable_sskey";
		}
	}
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *tab = ");
	emit_reg_accessor(fn, env, 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *key = ");
	emit_reg_accessor(fn, varname, 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *dst = ");
	emit_reg_accessor(fn, dst, 1);
	raviX_buffer_add_fstring(&fn->body, ";\n %s(L, tab, key, dst);\n ", fname);
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

/* Emit code for a variety of store table operations */
static int emit_op_store_table(struct function *fn, Instruction *insn)
{
	// FIXME what happens if key and value are both constants
	// Our pseudo reg will break I think
	const char *fname = "luaV_settable";
	if (insn->opcode == op_tput_ikey) {
		fname = "raviV_settable_i";
	} else if (insn->opcode == op_tput_skey) {
		fname = "raviV_settable_sskey";
	}
	Pseudo *env = get_target(insn, 0);
	Pseudo *varname = get_target(insn, 1);
	Pseudo *src = get_operand(insn, 0);
	if (varname->type == PSEUDO_CONSTANT && varname->constant->type == RAVI_TSTRING) {
		if (varname->constant->s->len < 40) {
			fname = "raviV_settable_sskey";
		}
	}
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *tab = ");
	emit_reg_accessor(fn, env, 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *key = ");
	emit_reg_accessor(fn, varname, 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *src = ");
	emit_reg_accessor(fn, src, 1);
	raviX_buffer_add_fstring(&fn->body, ";\n %s(L, tab, key, src);\n ", fname);
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

// From implementation point of view the main work is copy the registers to the
// right place. If we assume that at any time there is a 'fixed' stack size for the
// functions regular variables and temps and that when we call functions, we need
// to put the function followed by arguments on top of this 'fixed' stack.
// However the complication is that some of the arguments of the function may come
// from a previous function call and therefore may be occupying the same space!
// For example:
// local x = f()
// g(x, h())
// Here the return values from h() will be on the stack above the fixed stack space
// and g() expects x, followed by all the return values from h().
// But the nature of the byte code execution is that the return values of h()
// will be at the top of the fixed stack and will have offsets less than  the
// parameter positions of g() because when we call g() we will at least have the
// function value at the position of the first result from h(). Suppose the h() return values
// are at stack[10], stack[11], stack[12], etc.
// Then when we call g() we will put stack[10] = g, stack[11] = x,
// and stack[12] = stack[10], etc. To do this correctly we need to copy the
// last argument first.
static int emit_op_call(struct function *fn, Instruction *insn)
{
	assert(get_num_targets(insn) == 2);
	unsigned int n = get_num_operands(insn);
	// target register is where results should end up after the call
	// so it also tells us where we need to place the new frame
	// Note that this is typically a range starting at a register
	unsigned target_register = get_target(insn, 0)->regnum;
	// Number of values expected by the caller
	// If -1 it means all available values
	int nresults = (int)get_target(insn, 1)->constant->i;
	// I think it is okay to just use n as the check because if L->top was set
	// then n will be on top of that
	raviX_buffer_add_fstring(
	    &fn->body, " if (stackoverflow(L,%d)) { luaD_growstack(L, %d); base = ci->u.l.base; }\n", n + 1, n + 1);
	if (n > 1) {
		// We have function arguments (as n=0 is the function itself)
		Pseudo *last_arg = get_operand(insn, n - 1);
		if (last_arg->type == PSEUDO_RANGE) {
			// If last argument is a range that tells us that we need
			// to copy all available values from the register to L->top
			// But first check whether copy is necessary
			// suppose n = 2
			// then,
			// target_register[0] will have function
			// target_register[1] will have arg 1
			unsigned copy_to = target_register + n - 1;
			if (last_arg->regnum != copy_to) {
				raviX_buffer_add_string(&fn->body, "{\n");
				raviX_buffer_add_string(&fn->body, " TValue *src_base = ");
				emit_reg_accessor(fn, last_arg, 0);
				raviX_buffer_add_string(&fn->body, ";\n");
				raviX_buffer_add_string(&fn->body, " TValue *dest_base = ");
				Pseudo tmp = {.type = PSEUDO_TEMP_ANY, .regnum = copy_to};
				emit_reg_accessor(fn, &tmp, 0);
				raviX_buffer_add_string(&fn->body, ";\n TValue *src = L->top-1;\n");
				raviX_buffer_add_string(&fn->body, " L->top = dest_base + (L->top-src_base);\n");
				raviX_buffer_add_string(&fn->body, " TValue *dest = L->top-1;\n");
				raviX_buffer_add_string(&fn->body, " while (src >= src_base) {\n");
				raviX_buffer_add_string(&fn->body,
							"  dest->tt_ = src->tt_; dest->value_.gc = src->value_.gc;\n");
				raviX_buffer_add_string(&fn->body, "  src--;\n");
				raviX_buffer_add_string(&fn->body, "  dest--;\n");
				raviX_buffer_add_string(&fn->body, " }\n");
				raviX_buffer_add_string(&fn->body, "}\n");
			} else {
				// L->top stays where it is ...
			}
			n--; // discard the last arg
		} else {
			// L->top must be just past the last arg
			raviX_buffer_add_string(&fn->body, " L->top = ");
			emit_reg_accessor(fn, get_target(insn, 0), 0);
			raviX_buffer_add_fstring(&fn->body, " + %d;\n", n);
		}
	}
	// Copy the rest of the args
	for (int j = n - 1; j >= 0; j--) {
		Pseudo tmp = {.type = PSEUDO_TEMP_ANY, .regnum = target_register + j};
		emit_move(fn, get_operand(insn, j), &tmp);
	}
	// Call the function
	raviX_buffer_add_string(&fn->body, "{\n TValue *ra = ");
	emit_reg_accessor(fn, get_target(insn, 0), 0);
	raviX_buffer_add_fstring(&fn->body, ";\n int result = luaD_precall(L, ra, %d, 1);\n", nresults);
	raviX_buffer_add_string(&fn->body, " if (result) {\n");
	raviX_buffer_add_fstring(&fn->body, "  if (result == 1 && %d >= 0)\n", nresults);
	raviX_buffer_add_string(&fn->body, "   L->top = ci->top;\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	raviX_buffer_add_string(&fn->body, " else {  /* Lua function */\n");
	raviX_buffer_add_string(&fn->body, "  result = luaV_execute(L);\n");
	raviX_buffer_add_string(&fn->body, "  if (result) L->top = ci->top;\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	raviX_buffer_add_string(&fn->body, " base = ci->u.l.base;\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

/*
 * Output a C stack variable representing int/float value or constant
 */
static void emit_varname_or_constant(struct function *fn, Pseudo *pseudo)
{
	if (pseudo->type == PSEUDO_CONSTANT) {
		if (pseudo->constant->type == RAVI_TNUMINT) {
			raviX_buffer_add_fstring(&fn->body, "%lld", pseudo->constant->i);
		} else if (pseudo->constant->type == RAVI_TNUMFLT) {
			raviX_buffer_add_fstring(&fn->body, "%.16g", pseudo->constant->n);
		} else {
			assert(0);
		}
	} else if (pseudo->type == PSEUDO_TEMP_INT || pseudo->type == PSEUDO_TEMP_BOOL ||
		   pseudo->type == PSEUDO_TEMP_FLT) {
		emit_varname(pseudo, &fn->body);
	} else if (pseudo->type == PSEUDO_SYMBOL) {
		ravitype_t typecode = RAVI_TANY;
		if (pseudo->symbol->symbol_type == SYM_LOCAL) {
			typecode = pseudo->symbol->variable.value_type.type_code;
		} else if (pseudo->symbol->symbol_type == SYM_UPVALUE) {
			typecode = pseudo->symbol->upvalue.value_type.type_code;
		}
		if (typecode == RAVI_TNUMFLT) {
			raviX_buffer_add_string(&fn->body, "fltvalue(");
			emit_reg_accessor(fn, pseudo, 0);
			raviX_buffer_add_string(&fn->body, ")");
		} else if (typecode == RAVI_TNUMINT) {
			raviX_buffer_add_string(&fn->body, "ivalue(");
			emit_reg_accessor(fn, pseudo, 0);
			raviX_buffer_add_string(&fn->body, ")");
		} else {
			assert(0);
		}
	} else {
		assert(0);
	}
}

static int emit_comp_ii(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{ ");
	Pseudo *target = get_target(insn, 0);
	if (target->type == PSEUDO_TEMP_BOOL) {
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
	} else {
		raviX_buffer_add_string(&fn->body, "TValue *dst_reg = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, "; setbvalue(dst_reg, ");
	}
	const char *oper = NULL;
	switch (insn->opcode) {
	case op_eqii:
	case op_eqff:
		oper = "==";
		break;
	case op_ltii:
	case op_ltff:
		oper = "<";
		break;
	case op_leii:
	case op_leff:
		oper = "<=";
		break;
	default:
		assert(0);
		return -1;
	}
	emit_varname_or_constant(fn, get_operand(insn, 0));
	raviX_buffer_add_fstring(&fn->body, " %s ", oper);
	emit_varname_or_constant(fn, get_operand(insn, 1));
	if (target->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_string(&fn->body, "; }\n");
	} else {
		raviX_buffer_add_string(&fn->body, "); }\n");
	}
	return 0;
}

static int emit_bin_ii(struct function *fn, Instruction *insn)
{
	// FIXME - needs to also work with typed function params
	raviX_buffer_add_string(&fn->body, "{ ");
	Pseudo *target = get_target(insn, 0);
	if (target->type == PSEUDO_TEMP_FLT || target->type == PSEUDO_TEMP_INT || target->type == PSEUDO_TEMP_BOOL) {
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
	} else {
		raviX_buffer_add_string(&fn->body, "TValue *dst_reg = ");
		emit_reg_accessor(fn, target, 0);
		if (insn->opcode == op_addff || insn->opcode == op_subff || insn->opcode == op_mulff ||
		    insn->opcode == op_divff) {
			raviX_buffer_add_string(&fn->body, "; setfltvalue(dst_reg, ");
		} else {
			raviX_buffer_add_string(&fn->body, "; setivalue(dst_reg, ");
		}
	}
	const char *oper = NULL;
	switch (insn->opcode) {
	case op_addff:
	case op_addii:
		oper = "+";
		break;

	case op_subff:
	case op_subii:
		oper = "-";
		break;

	case op_mulff:
	case op_mulii:
		oper = "*";
		break;

	case op_divff:
	case op_divii:
		oper = "/";
		break;

	case op_bandii:
		oper = "&";
		break;

	case op_borii:
		oper = "|";
		break;

	case op_bxorii:
		oper = "^";
		break;
	default:
		assert(0);
		return -1;
	}
	emit_varname_or_constant(fn, get_operand(insn, 0));
	raviX_buffer_add_fstring(&fn->body, " %s ", oper);
	emit_varname_or_constant(fn, get_operand(insn, 1));
	if (target->type == PSEUDO_TEMP_FLT || target->type == PSEUDO_TEMP_INT || target->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_string(&fn->body, "; }\n");
	} else {
		raviX_buffer_add_string(&fn->body, "); }\n");
	}
	return 0;
}

static int emit_bitop_ii(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n ");
	Pseudo *target = get_target(insn, 0);
	if (target->type == PSEUDO_TEMP_FLT || target->type == PSEUDO_TEMP_INT || target->type == PSEUDO_TEMP_BOOL) {
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
	} else {
		raviX_buffer_add_string(&fn->body, "TValue *dst_reg = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, "; setivalue(dst_reg, ");
	}
	raviX_buffer_add_string(&fn->body, "luaV_shiftl(");
	emit_varname_or_constant(fn, get_operand(insn, 0));
	if (insn->opcode == op_shlii)
		raviX_buffer_add_string(&fn->body, ", ");
	else if (insn->opcode == op_shrii)
		raviX_buffer_add_string(&fn->body, ", -");
	else {
		assert(0);
		return -1;
	}
	emit_varname_or_constant(fn, get_operand(insn, 1));
	raviX_buffer_add_string(&fn->body, ")");
	if (target->type == PSEUDO_TEMP_FLT || target->type == PSEUDO_TEMP_INT || target->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_string(&fn->body, ";\n}\n");
	} else {
		raviX_buffer_add_string(&fn->body, ");\n}\n");
	}
	return 0;
}

static int emit_bin_fi(struct function *fn, Instruction *insn)
{
	// FIXME - needs to also work with typed function params
	raviX_buffer_add_string(&fn->body, "{ ");
	Pseudo *target = get_target(insn, 0);
	if (target->type == PSEUDO_TEMP_FLT) {
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
	} else {
		raviX_buffer_add_string(&fn->body, "TValue *dst_reg = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, "; setfltvalue(dst_reg, ");
	}
	const char *oper = NULL;
	switch (insn->opcode) {
	case op_addfi:
		oper = "+";
		break;

	case op_subfi:
		oper = "-";
		break;

	case op_mulfi:
		oper = "*";
		break;

	case op_divfi:
		oper = "/";
		break;

	default:
		assert(0);
		return -1;
	}
	emit_varname_or_constant(fn, get_operand(insn, 0));
	raviX_buffer_add_fstring(&fn->body, " %s ((lua_Number)(", oper);
	emit_varname_or_constant(fn, get_operand(insn, 1));
	raviX_buffer_add_string(&fn->body, "))");
	if (target->type == PSEUDO_TEMP_FLT) {
		raviX_buffer_add_string(&fn->body, "; }\n");
	} else {
		raviX_buffer_add_string(&fn->body, "); }\n");
	}
	return 0;
}

static int emit_bin_if(struct function *fn, Instruction *insn)
{
	// FIXME - needs to also work with typed function params
	raviX_buffer_add_string(&fn->body, "{ ");
	Pseudo *target = get_target(insn, 0);
	if (target->type == PSEUDO_TEMP_FLT) {
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = ");
	} else {
		raviX_buffer_add_string(&fn->body, "TValue *dst_reg = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, "; setfltvalue(dst_reg, ");
	}
	const char *oper = NULL;
	switch (insn->opcode) {
	case op_subif:
		oper = "-";
		break;

	case op_divif:
		oper = "/";
		break;

	default:
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "((lua_Number)(");
	emit_varname_or_constant(fn, get_operand(insn, 0));
	raviX_buffer_add_fstring(&fn->body, ")) %s ", oper);
	emit_varname_or_constant(fn, get_operand(insn, 1));
	if (target->type == PSEUDO_TEMP_FLT) {
		raviX_buffer_add_string(&fn->body, "; }\n");
	} else {
		raviX_buffer_add_string(&fn->body, "); }\n");
	}
	return 0;
}

static int emit_op_arrayget_ikey(struct function *fn, Instruction *insn)
{
	const char *array_type = insn->opcode == op_iaget_ikey ? "lua_Integer *" : "lua_Number *";
	const char *setterfunc = insn->opcode == op_iaget_ikey ? "setivalue" : "setfltvalue";
	unsigned type = insn->opcode == op_iaget_ikey ? PSEUDO_TEMP_INT : PSEUDO_TEMP_FLT;
	Pseudo *arr = get_operand(insn, 0);
	Pseudo *key = get_operand(insn, 1);
	Pseudo *dst = get_target(insn, 0);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " RaviArray *arr = arrvalue(");
	emit_reg_accessor(fn, arr, 0);
	raviX_buffer_add_string(&fn->body, ");\n lua_Unsigned ukey = (lua_Unsigned) ");
	if (key->type == PSEUDO_CONSTANT) {
		raviX_buffer_add_fstring(&fn->body, "%lld", key->constant->i);
	} else if (key->type == PSEUDO_TEMP_INT) {
		emit_varname(key, &fn->body);
	} else if (key->type == PSEUDO_SYMBOL) {
		// this must be an integer
		raviX_buffer_add_string(&fn->body, "ivalue(");
		emit_reg_accessor(fn, key, 0);
		raviX_buffer_add_string(&fn->body, ")");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_fstring(&fn->body, " %siptr = (%s)arr->data;\n ", array_type, array_type);
	if (dst->type == type) {
		emit_varname(dst, &fn->body);
		raviX_buffer_add_string(&fn->body, " = iptr[ukey];\n");
	} else if (dst->type == PSEUDO_TEMP_ANY || dst->type == PSEUDO_SYMBOL || dst->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_string(&fn->body, "TValue *dest_reg = ");
		emit_reg_accessor(fn, dst, 0);
		raviX_buffer_add_fstring(&fn->body, "; %s(dest_reg, iptr[ukey]);\n", setterfunc);
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_arrayput_val(struct function *fn, Instruction *insn)
{
	const char *array_type = insn->opcode == op_iaput_ival ? "lua_Integer *" : "lua_Number *";
	const char *getterfunc = insn->opcode == op_iaput_ival ? "ivalue" : "fltvalue";
	const char *setterfunc = insn->opcode == op_iaput_ival ? "raviH_set_int" : "raviH_set_float";
	unsigned type = insn->opcode == op_iaput_ival ? PSEUDO_TEMP_INT : PSEUDO_TEMP_FLT;
	Pseudo *arr = get_target(insn, 0);
	Pseudo *key = get_target(insn, 1);
	Pseudo *src = get_operand(insn, 0);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " RaviArray *arr = arrvalue(");
	emit_reg_accessor(fn, arr, 0);
	raviX_buffer_add_string(&fn->body, ");\n lua_Unsigned ukey = (lua_Unsigned) ");
	if (key->type == PSEUDO_CONSTANT) {
		raviX_buffer_add_fstring(&fn->body, "%lld", key->constant->i);
	} else if (key->type == PSEUDO_TEMP_INT) {
		emit_varname(key, &fn->body);
	} else if (key->type == PSEUDO_SYMBOL) {
		// this must be an integer
		raviX_buffer_add_string(&fn->body, "ivalue(");
		emit_reg_accessor(fn, key, 0);
		raviX_buffer_add_string(&fn->body, ")");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_fstring(&fn->body, " %siptr = (%s)arr->data;\n ", array_type, array_type);
	raviX_buffer_add_string(&fn->body, "if (ukey < (lua_Unsigned)(arr->len)) {\n");
	raviX_buffer_add_string(&fn->body, " iptr[ukey] = ");
	if (src->type == type) {
		emit_varname(src, &fn->body);
	} else if (src->type == PSEUDO_TEMP_ANY || src->type == PSEUDO_SYMBOL || src->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_fstring(&fn->body, "%s(", getterfunc);
		emit_reg_accessor(fn, src, 0);
		raviX_buffer_add_string(&fn->body, ")");
	} else if (src->type == PSEUDO_CONSTANT) {
		if (src->constant->type == RAVI_TNUMINT) {
			raviX_buffer_add_fstring(&fn->body, "%lld", src->constant->i);
		} else {
			raviX_buffer_add_fstring(&fn->body, "%g", src->constant->n);
		}
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, ";\n} else {\n");
	raviX_buffer_add_fstring(&fn->body, " %s(L, arr, ukey, ", setterfunc);
	if (src->type == type) {
		emit_varname(src, &fn->body);
	} else if (src->type == PSEUDO_TEMP_ANY || src->type == PSEUDO_SYMBOL || src->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_fstring(&fn->body, "%s(", getterfunc);
		emit_reg_accessor(fn, src, 0);
		raviX_buffer_add_string(&fn->body, ")");
	} else if (src->type == PSEUDO_CONSTANT) {
		if (src->constant->type == RAVI_TNUMINT) {
			raviX_buffer_add_fstring(&fn->body, "%lld", src->constant->i);
		} else {
			raviX_buffer_add_fstring(&fn->body, "%g", src->constant->n);
		}
	}
	raviX_buffer_add_string(&fn->body, ");\n");
	raviX_buffer_add_string(&fn->body, "}\n}\n");
	return 0;
}

static int emit_op_totype(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	if (insn->opcode == op_toiarray) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisiarray(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_integer_array_expected);
	} else if (insn->opcode == op_tofarray) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisfarray(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_number_array_expected);
	} else if (insn->opcode == op_totable) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisLtable(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_table_expected);
	} else if (insn->opcode == op_toclosure) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisclosure(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_closure_expected);
	} else if (insn->opcode == op_tostring) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisstring(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_string_expected);
	} else if (insn->opcode == op_toint) {
		raviX_buffer_add_string(&fn->body, ";\n if (!ttisinteger(ra)) {\n");
		raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_integer_expected);
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "  goto Lraise_error;\n");
	raviX_buffer_add_string(&fn->body, " }\n}\n");
	return 0;
}

static int emit_op_toflt(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n lua_Number n = 0;\n");
	raviX_buffer_add_string(&fn->body, " if (ttisnumber(ra)) { n = (ttisinteger(ra) ? (double) ivalue(ra) : "
					   "fltvalue(ra)); setfltvalue(ra, n); }\n");
	raviX_buffer_add_string(&fn->body, " else {\n");
	raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_number_expected);
	raviX_buffer_add_string(&fn->body, "  goto Lraise_error;\n");
	raviX_buffer_add_string(&fn->body, " }\n}\n");
	return 0;
}

static int emit_op_tousertype(struct function *fn, Instruction *insn)
{
	Pseudo *type_name = get_first_operand(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n if (!ttisnil(ra)) {\n");
	raviX_buffer_add_string(&fn->body, "  TValue *rb = ");
	emit_reg_accessor(fn, type_name, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body,
				"  if (!ttisshrstring(rb) || !raviV_check_usertype(L, tsvalue(rb), ra)) {\n");
	raviX_buffer_add_fstring(&fn->body, "   error_code = %d;\n", Error_type_mismatch);
	raviX_buffer_add_string(&fn->body, "   goto Lraise_error;\n");
	raviX_buffer_add_string(&fn->body, "  }\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_newtable(struct function *fn, Instruction *insn)
{
	Pseudo *target_pseudo = get_first_target(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, target_pseudo, 0);
	raviX_buffer_add_string(&fn->body, ";\n raviV_op_newtable(L, ci, ra, 0, 0);\n");
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_newarray(struct function *fn, Instruction *insn)
{
	Pseudo *target_pseudo = get_first_target(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, target_pseudo, 0);
	raviX_buffer_add_fstring(&fn->body, ";\n %s(L, ci, ra);\n",
				 insn->opcode == op_newfarray ? "raviV_op_newarrayfloat" : "raviV_op_newarrayint");
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_closure(struct function *fn, Instruction *insn)
{
	Pseudo *closure_pseudo = get_first_operand(insn);
	Pseudo *target_pseudo = get_first_target(insn);

	assert(closure_pseudo->type == PSEUDO_PROC);
	Proc *proc = closure_pseudo->proc;
	Proc *parent_proc = proc->parent;
	Proc *cursor;
	int parent_index = -1;
	int i = 0;
	FOR_EACH_PTR(parent_proc->procs, Proc, cursor)
	{
		if (cursor->id == proc->id) {
			assert(cursor == proc);
			parent_index = i;
			break;
		}
		i++;
	}
	END_FOR_EACH_PTR(cursor);
	if (parent_index == -1) {
		assert(0);
		return -1;
	}
	unsigned reg = compute_register_from_base(fn, target_pseudo);
	raviX_buffer_add_fstring(&fn->body, "raviV_op_closure(L, ci, cl, %d, %d);\n", reg, parent_index);
	emit_reload_base(fn);
	return 0;
}

static int emit_op_close(struct function *fn, Instruction *insn)
{
	Pseudo *pseudo = get_first_operand(insn);
	raviX_buffer_add_string(&fn->body, "{\n TValue *clsvar = ");
	emit_reg_accessor(fn, pseudo, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
#ifdef RAVI_DEFER_STATEMENT
	raviX_buffer_add_string(&fn->body, " luaF_close(L, clsvar, LUA_OK);\n");
	emit_reload_base(fn);
#else
	raviX_buffer_add_string(&fn->body, " luaF_close(L, clsvar);\n");
#endif
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_len(struct function *fn, Instruction *insn)
{
	Pseudo *obj = get_first_operand(insn);
	Pseudo *target = get_first_target(insn);
	raviX_buffer_add_string(&fn->body, "{\n TValue *len = ");
	emit_reg_accessor(fn, target, 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *obj = ");
	emit_reg_accessor(fn, obj, 0);
	raviX_buffer_add_string(&fn->body, ";\n luaV_objlen(L, len, obj);\n");
	emit_reload_base(fn);
	if (target->type == PSEUDO_TEMP_INT) {
		raviX_buffer_add_string(&fn->body, " ");
		emit_varname_or_constant(fn, target);
		raviX_buffer_add_string(&fn->body, " = ival0.value_.i;\n"); // FIXME use some accessor
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_generic_comp(struct function *fn, Instruction *insn)
{
	const char *oper = "==";
	if (insn->opcode == op_lt) {
		oper = "<";
	} else if (insn->opcode == op_le) {
		oper = "<=";
	}
	const char *comparison_function =
	    (insn->opcode == op_eq) ? "luaV_equalobj" : ((insn->opcode == op_lt) ? "luaV_lessthan" : "luaV_lessequal");
	raviX_buffer_add_string(&fn->body, "{\n int result = 0;\n");
	raviX_buffer_add_string(&fn->body, " TValue *rb = ");
	emit_reg_accessor(fn, get_operand(insn, 0), 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *rc = ");
	emit_reg_accessor(fn, get_operand(insn, 1), 1);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " if (ttisinteger(rb) && ttisinteger(rc))\n");
	raviX_buffer_add_fstring(&fn->body, "  result = (ivalue(rb) %s ivalue(rc));\n", oper);
	raviX_buffer_add_string(&fn->body, " else {\n");
	raviX_buffer_add_fstring(&fn->body, "  result = %s(L, rb, rc);\n  ", comparison_function);
	// Reload pointer to base as the call to luaV_equalobj() may
	// have invoked a Lua function and as a result the stack may have
	// been reallocated - so the previous base pointer could be stale
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, " }\n");
	Pseudo *target = get_first_target(insn);
	if (target->type == PSEUDO_TEMP_ANY) {
		raviX_buffer_add_string(&fn->body, " setbvalue(");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, ", result != 0);\n");
	} else if (target->type == PSEUDO_TEMP_BOOL) {
		raviX_buffer_add_string(&fn->body, " ");
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = result != 0;\n");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_arith(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n");
	Pseudo *target = get_target(insn, 0);
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, target, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	const char *oper = NULL;
	const char *tm = NULL;
	switch (insn->opcode) {
	case op_add:
		oper = "+";
		tm = "TM_ADD";
		break;

	case op_sub:
		oper = "-";
		tm = "TM_SUB";
		break;

	case op_mul:
		oper = "*";
		tm = "TM_MUL";
		break;

	default:
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, " TValue *rb = ");
	emit_reg_accessor(fn, get_operand(insn, 0), 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " TValue *rc = ");
	emit_reg_accessor(fn, get_operand(insn, 1), 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " lua_Integer i = 0;\n");
	raviX_buffer_add_string(&fn->body, " lua_Integer ic = 0;\n");
	raviX_buffer_add_string(&fn->body, " lua_Number n = 0.0;\n");
	raviX_buffer_add_string(&fn->body, " lua_Number nc = 0.0;\n");

	raviX_buffer_add_string(&fn->body, " if (ttisinteger(rb) && ttisinteger(rc)) {\n");
	raviX_buffer_add_string(&fn->body, "  i = ivalue(rb);\n");
	raviX_buffer_add_string(&fn->body, "  ic = ivalue(rc);\n");
	raviX_buffer_add_fstring(&fn->body, "  setivalue(ra, (i %s ic));\n", oper);
	raviX_buffer_add_string(&fn->body, " } else if (tonumberns(rb, n) && tonumberns(rc, nc)) {\n");
	raviX_buffer_add_fstring(&fn->body, "  setfltvalue(ra, (n %s nc));\n", oper);
	raviX_buffer_add_string(&fn->body, " } else {\n");
	raviX_buffer_add_fstring(&fn->body, "  luaT_trybinTM(L, rb, rc, ra, %s);\n", tm);
	raviX_buffer_add_string(&fn->body, "  base = ci->u.l.base;\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_not(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *rb = ");
	emit_reg_accessor(fn, get_first_operand(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n int result = l_isfalse(rb);\n");
	raviX_buffer_add_string(&fn->body, " setbvalue(ra, result);\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_bnot(struct function *fn, Instruction *insn)
{
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *rb = ");
	emit_reg_accessor(fn, get_first_operand(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n raviV_op_bnot(L, ra, rb);\n");
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

/*
 * Following generates code that uses luaO_arith() calls
 * so not very efficient.
 */
static int emit_op_binary(struct function *fn, Instruction *insn)
{
	int op = 0;
	switch (insn->opcode) {
	case op_div:
		op = LUA_OPDIV;
		break;
	case op_idiv:
		op = LUA_OPIDIV;
		break;
	case op_band:
		op = LUA_OPBAND;
		break;
	case op_bor:
		op = LUA_OPBOR;
		break;
	case op_bxor:
		op = LUA_OPBXOR;
		break;
	case op_shl:
		op = LUA_OPSHL;
		break;
	case op_shr:
		op = LUA_OPSHR;
		break;
	case op_mod:
		op = LUA_OPMOD;
		break;
	case op_pow:
		op = LUA_OPPOW;
		break;
	default:
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, get_first_target(insn), 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *rb = ");
	emit_reg_accessor(fn, get_operand(insn, 0), 0);
	raviX_buffer_add_string(&fn->body, ";\n TValue *rc = ");
	emit_reg_accessor(fn, get_operand(insn, 1), 1);
	raviX_buffer_add_fstring(&fn->body, ";\n luaO_arith(L, %d, rb, rc, ra);\n", op);
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_unmi_unmf(struct function *fn, Instruction *insn)
{
	Pseudo *target = get_first_target(insn);
	Pseudo *operand = get_first_operand(insn);
	int type = insn->opcode == op_unmi ? PSEUDO_TEMP_INT : PSEUDO_TEMP_FLT;
	const char *setter = insn->opcode == op_unmi ? "setivalue" : "setfltvalue";
	const char *getter = insn->opcode == op_unmi ? "ivalue" : "fltvalue";
	raviX_buffer_add_string(&fn->body, "{\n");
	if (operand->type != type && operand->type != PSEUDO_CONSTANT) {
		raviX_buffer_add_string(&fn->body, " TValue *rb = ");
		emit_reg_accessor(fn, operand, 0);
		raviX_buffer_add_string(&fn->body, ";\n");
	}
	if (target->type == type) {
		raviX_buffer_add_string(&fn->body, " ");
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = -");
		if (operand->type != type && operand->type != PSEUDO_CONSTANT) {
			raviX_buffer_add_fstring(&fn->body, "%s(rb)", getter);
		} else {
			emit_varname_or_constant(fn, operand);
		}
		raviX_buffer_add_string(&fn->body, ";\n");
	} else if (target->type == PSEUDO_TEMP_ANY || target->type == PSEUDO_SYMBOL ||
		   target->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_string(&fn->body, " TValue *ra = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_fstring(&fn->body, ";\n %s(ra, ", setter);
		if (operand->type != type && operand->type != PSEUDO_CONSTANT) {
			raviX_buffer_add_fstring(&fn->body, "%s(rb)", getter);
		} else {
			emit_varname_or_constant(fn, operand);
		}
		raviX_buffer_add_string(&fn->body, ");\n");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_unm(struct function *fn, Instruction *insn)
{
	Pseudo *target = get_first_target(insn);
	Pseudo *operand = get_first_operand(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " lua_Number n = 0.0;\n");
	raviX_buffer_add_string(&fn->body, " TValue *rb = ");
	emit_reg_accessor(fn, operand, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " TValue *ra = ");
	emit_reg_accessor(fn, target, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " if (ttisinteger(rb)) {\n");
	raviX_buffer_add_string(&fn->body, "  lua_Integer i = ivalue(rb);\n");
	raviX_buffer_add_string(&fn->body, "  setivalue(ra, intop(-, 0, i));\n");
	raviX_buffer_add_string(&fn->body, " } else if (tonumberns(rb, n)) {\n");
	raviX_buffer_add_string(&fn->body, "  setfltvalue(ra, luai_numunm(L, n));\n");
	raviX_buffer_add_string(&fn->body, " } else {\n");
	raviX_buffer_add_string(&fn->body, "  luaT_trybinTM(L, rb, rb, ra, TM_UNM);\n");
	emit_reload_base(fn);
	raviX_buffer_add_string(&fn->body, " }\n");
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_movfi(struct function *fn, Instruction *insn)
{
	Pseudo *target = get_first_target(insn);
	Pseudo *operand = get_first_operand(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *rb = ");
	emit_reg_accessor(fn, operand, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " lua_Integer i = 0;\n");
	raviX_buffer_add_string(&fn->body, " if (!tointeger(rb, &i)) {\n");
	raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_integer_expected);
	raviX_buffer_add_string(&fn->body, "  goto Lraise_error;\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	if (target->type == PSEUDO_TEMP_INT) {
		raviX_buffer_add_string(&fn->body, " ");
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = i;\n");
	} else if (target->type == PSEUDO_TEMP_ANY || target->type == PSEUDO_SYMBOL ||
		   target->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_string(&fn->body, " TValue *ra = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, ";\n setivalue(ra, i);\n");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int emit_op_movif(struct function *fn, Instruction *insn)
{
	Pseudo *target = get_first_target(insn);
	Pseudo *operand = get_first_operand(insn);
	raviX_buffer_add_string(&fn->body, "{\n");
	raviX_buffer_add_string(&fn->body, " TValue *rb = ");
	emit_reg_accessor(fn, operand, 0);
	raviX_buffer_add_string(&fn->body, ";\n");
	raviX_buffer_add_string(&fn->body, " lua_Number n = 0.0;\n");
	raviX_buffer_add_string(&fn->body, " if (!tonumber(rb, &n)) {\n");
	raviX_buffer_add_fstring(&fn->body, "  error_code = %d;\n", Error_number_expected);
	raviX_buffer_add_string(&fn->body, "  goto Lraise_error;\n");
	raviX_buffer_add_string(&fn->body, " }\n");
	if (target->type == PSEUDO_TEMP_FLT) {
		raviX_buffer_add_string(&fn->body, " ");
		emit_varname(target, &fn->body);
		raviX_buffer_add_string(&fn->body, " = n;\n");
	} else if (target->type == PSEUDO_TEMP_ANY || target->type == PSEUDO_SYMBOL ||
		   target->type == PSEUDO_LUASTACK) {
		raviX_buffer_add_string(&fn->body, " TValue *ra = ");
		emit_reg_accessor(fn, target, 0);
		raviX_buffer_add_string(&fn->body, ";\n setfltvalue(ra, n);\n");
	} else {
		assert(0);
		return -1;
	}
	raviX_buffer_add_string(&fn->body, "}\n");
	return 0;
}

static int output_instruction(struct function *fn, Instruction *insn)
{
	int rc = 0;
	switch (insn->opcode) {
	case op_ret:
		rc = emit_op_ret(fn, insn);
		break;
	case op_br:
		rc = emit_op_br(fn, insn);
		break;
	case op_cbr:
		rc = emit_op_cbr(fn, insn);
		break;
	case op_mov:
	case op_movi:
	case op_movf:
		rc = emit_op_mov(fn, insn);
		break;
	case op_movfi:
		rc = emit_op_movfi(fn, insn);
		break;
	case op_movif:
		rc = emit_op_movif(fn, insn);
		break;
	case op_loadglobal:
	case op_get:
	case op_get_skey:
	case op_get_ikey:
	case op_tget_skey:
	case op_tget_ikey:
	case op_tget: /* TODO custom codegen */
	case op_iaget:
	case op_faget:
		rc = emit_op_load_table(fn, insn);
		break;
	case op_storeglobal:
	case op_put:
	case op_put_skey:
	case op_put_ikey:
	case op_tput_skey:
	case op_tput_ikey:
	case op_tput: /* TODO custom codegen */
	case op_iaput:
	case op_faput:
		rc = emit_op_store_table(fn, insn);
		break;
	case op_call:
		rc = emit_op_call(fn, insn);
		break;

	case op_addff:
	case op_subff:
	case op_mulff:
	case op_divff:

	case op_addii:
	case op_subii:
	case op_mulii:
	case op_divii:
	case op_bandii:
	case op_borii:
	case op_bxorii:
		rc = emit_bin_ii(fn, insn);
		break;

	case op_shlii:
	case op_shrii:
		rc = emit_bitop_ii(fn, insn);
		break;

	case op_eqii:
	case op_ltii:
	case op_leii:
	case op_eqff:
	case op_ltff:
	case op_leff:
		rc = emit_comp_ii(fn, insn);
		break;

	case op_addfi:
	case op_subfi:
	case op_mulfi:
	case op_divfi:
		rc = emit_bin_fi(fn, insn);
		break;

	case op_subif:
	case op_divif:
		rc = emit_bin_if(fn, insn);
		break;

	case op_add:
	case op_sub:
	case op_mul:
		rc = emit_op_arith(fn, insn);
		break;

	case op_not:
		rc = emit_op_not(fn, insn);
		break;

	case op_bnot:
		rc = emit_op_bnot(fn, insn);
		break;

	case op_div:
	case op_idiv:
	case op_band:
	case op_bor:
	case op_bxor:
	case op_shl:
	case op_shr:
	case op_mod:
	case op_pow:
		rc = emit_op_binary(fn, insn);
		break;

	case op_unmi:
	case op_unmf:
		rc = emit_op_unmi_unmf(fn, insn);
		break;

	case op_unm:
		rc = emit_op_unm(fn, insn);
		break;

		// case op_leni:

		// op_string_concat

	case op_eq:
	case op_lt:
	case op_le:
		rc = emit_generic_comp(fn, insn);
		break;

	case op_iaget_ikey:
	case op_faget_ikey:
		rc = emit_op_arrayget_ikey(fn, insn);
		break;

	case op_iaput_ival:
	case op_faput_fval:
		rc = emit_op_arrayput_val(fn, insn);
		break;

	case op_toiarray:
	case op_tofarray:
	case op_totable:
	case op_tostring:
	case op_toclosure:
	case op_toint:
		rc = emit_op_totype(fn, insn);
		break;

	case op_toflt:
		rc = emit_op_toflt(fn, insn);
		break;

	case op_totype:
		rc = emit_op_tousertype(fn, insn);
		break;

	case op_closure:
		rc = emit_op_closure(fn, insn);
		break;

	case op_newtable:
		rc = emit_op_newtable(fn, insn);
		break;

	case op_newiarray:
		rc = emit_op_newarray(fn, insn);
		break;

	case op_newfarray:
		rc = emit_op_newarray(fn, insn);
		break;

	case op_close:
		rc = emit_op_close(fn, insn);
		break;

	case op_len:
	case op_leni:
		rc = emit_op_len(fn, insn);
		break;

	default:
		fprintf(stderr, "Unsupported opcode %s\n", raviX_opcode_name(insn->opcode));
		rc = -1;
	}
	return rc;
}

static int output_instructions(struct function *fn, InstructionList *list)
{
	Instruction *insn;
	int rc = 0;
	FOR_EACH_PTR(list, Instruction, insn)
	{
		rc = output_instruction(fn, insn);
		if (rc != 0)
			break;
	}
	END_FOR_EACH_PTR(insn)
	return rc;
}

static inline bool is_block_deleted(BasicBlock *bb)
{
	return bb->index != ENTRY_BLOCK && bb->index != EXIT_BLOCK && get_num_instructions(bb) == 0;
	// block was logically deleted if it has got zero instructions and
	// it isn't the entry/exit block.
}

static int output_basic_block(struct function *fn, BasicBlock *bb)
{
	if (is_block_deleted(bb))
		return 0;
	int rc = 0;
	raviX_buffer_add_fstring(&fn->body, "L%d:\n", bb->index);
	if (bb->index == ENTRY_BLOCK) {
	} else if (bb->index == EXIT_BLOCK) {
	} else {
	}
	rc = output_instructions(fn, bb->insns);
	if (bb->index == EXIT_BLOCK) {
		raviX_buffer_add_string(&fn->body, " return result;\n");
		raviX_buffer_add_string(&fn->body, "Lraise_error:\n");
		raviX_buffer_add_string(&fn->body, " raviV_raise_error(L, error_code); /* does not return */\n");
		raviX_buffer_add_string(&fn->body, " return result;\n");
	}
	return rc;
}

static inline unsigned get_num_params(Proc *proc)
{
	return raviX_ptrlist_size((const PtrList *)proc->function_expr->function_expr.args);
}

static inline unsigned get_num_upvalues(Proc *proc)
{
	return raviX_ptrlist_size((const PtrList *)proc->function_expr->function_expr.upvalues);
}

/* Generate code for setting up a Lua Proto structure, recursively for each child function */
static int generate_lua_proc(Proc *proc, TextBuffer *mb)
{
	raviX_buffer_add_fstring(mb, " f->ravi_jit.jit_function = %s;\n", proc->funcname);
	raviX_buffer_add_string(mb, " f->ravi_jit.jit_status = RAVI_JIT_COMPILED;\n");
	raviX_buffer_add_fstring(mb, " f->numparams = %u;\n", get_num_params(proc));
	raviX_buffer_add_fstring(mb, " f->is_vararg = 0;\n"); // FIXME Var arg not supported yet
	raviX_buffer_add_fstring(mb, " f->maxstacksize = %u;\n", compute_max_stack_size(proc));

	// Load constants - we only need to load string constants as integer/floats are coded in
	raviX_buffer_add_fstring(mb, " f->k = luaM_newvector(L, %u, TValue);\n", proc->num_strconstants);
	raviX_buffer_add_fstring(mb, " f->sizek = %u;\n", proc->num_strconstants);
	raviX_buffer_add_fstring(mb, " for (int i = 0; i < %u; i++)\n", proc->num_strconstants);
	raviX_buffer_add_string(
	    mb, "  setnilvalue(&f->k[i]);\n"); // Do this in case there is a problem allocating the strings
	SetEntry *entry;
	set_foreach(proc->constants, entry)
	{
		const Constant *constant = (Constant *)entry->key;
		// We only need to register string constants
		if (constant->type == RAVI_TSTRING) {
			raviX_buffer_add_fstring(mb, " {\n  TValue *o = &f->k[%u];\n", constant->index);
			if (constant->s->len == 0) {
				raviX_buffer_add_string(mb, "  o = NULL;\n");
			} else {
				// FIXME we need to escape chars?
				raviX_buffer_add_fstring(mb, "  setsvalue2n(L, o, luaS_newlstr(L, \"%.*s\", %u));\n",
							 constant->s->len, constant->s->str, constant->s->len);
			}
			raviX_buffer_add_string(mb, " }\n");
		}
	}

	// Load up-values
	raviX_buffer_add_fstring(mb, " f->upvalues = luaM_newvector(L, %u, Upvaldesc);\n", get_num_upvalues(proc));
	raviX_buffer_add_fstring(mb, " f->sizeupvalues = %u;\n", get_num_upvalues(proc));
	int i = 0;
	LuaSymbol *sym;
	FOR_EACH_PTR(proc->function_expr->function_expr.upvalues, LuaSymbol, sym)
	{
		raviX_buffer_add_fstring(mb, " f->upvalues[%u].instack = %u;\n", i, sym->upvalue.is_in_parent_stack);
		raviX_buffer_add_fstring(mb, " f->upvalues[%u].idx = %u;\n", i, sym->upvalue.parent_upvalue_index);
		raviX_buffer_add_fstring(mb, " f->upvalues[%u].name = NULL;\n", i);
		raviX_buffer_add_fstring(mb, " f->upvalues[%u].usertype = NULL;\n", i);
		raviX_buffer_add_fstring(mb, " f->upvalues[%u].ravi_type = %d;\n", i,
					 sym->upvalue.value_type.type_code);
		i++;
	}
	END_FOR_EACH_PTR(sym);

	// Load child protos recursively
	if (get_num_childprocs(proc) > 0) {
		raviX_buffer_add_fstring(mb, " f->p = luaM_newvector(L, %u, Proto *);\n", get_num_childprocs(proc));
		raviX_buffer_add_fstring(mb, " f->sizep = %u;\n", get_num_childprocs(proc));
		raviX_buffer_add_fstring(mb, " for (int i = 0; i < %u; i++)\n", get_num_childprocs(proc));
		raviX_buffer_add_string(mb, "   f->p[i] = NULL;\n");
		Proc *childproc;
		i = 0;
		FOR_EACH_PTR(proc->procs, Proc, childproc)
		{
			raviX_buffer_add_fstring(mb, " f->p[%u] = luaF_newproto(L);\n", i);
			raviX_buffer_add_string(mb, "{ \n");
			raviX_buffer_add_fstring(mb, " Proto *parent = f; f = f->p[%u];\n", i);
			generate_lua_proc(childproc, mb);
			raviX_buffer_add_string(mb, " f = parent;\n");
			raviX_buffer_add_string(mb, "}\n");
			i++;
		}
		END_FOR_EACH_PTR(childproc);
	}
	return 0;
}

/* Generate the equivalent of a luaU_undump such that when called from Lua/Ravi code
 * it will build the closure encapsulating the Lua chunk.
 */
static int generate_lua_closure(Proc *proc, const char *funcname, TextBuffer *mb)
{
	raviX_buffer_add_fstring(mb, "EXPORT LClosure *%s(lua_State *L) {\n", funcname);
	raviX_buffer_add_fstring(mb, " LClosure *cl = luaF_newLclosure(L, %u);\n", get_num_upvalues(proc));
	raviX_buffer_add_string(mb, " setclLvalue(L, L->top, cl);\n");
	raviX_buffer_add_string(mb, " luaD_inctop(L);\n");
	raviX_buffer_add_string(mb, " cl->p = luaF_newproto(L);\n");
	raviX_buffer_add_string(mb, " Proto *f = cl->p;\n");
	generate_lua_proc(proc, mb);
	raviX_buffer_add_string(mb, " return cl;\n");
	raviX_buffer_add_string(mb, "}\n");
	return 0;
}

/* Generate C code for each proc recursively */
static int generate_C_code(struct Ravi_CompilerInterface *ravi_interface, Proc *proc, TextBuffer *mb)
{
	int rc = 0;
	struct function fn;

	initfn(&fn, proc, ravi_interface);

	BasicBlock *bb;
	for (int i = 0; i < (int)proc->node_count; i++) {
		bb = proc->nodes[i];
		rc = output_basic_block(&fn, bb);
		if (rc != 0)
			break;
	}

	raviX_buffer_add_string(&fn.body, "}\n");
	raviX_buffer_add_string(mb, fn.prologue.buf);
	raviX_buffer_add_string(mb, fn.body.buf);
	cleanup(&fn);

	if (rc != 0)
		return rc;

	Proc *childproc;
	FOR_EACH_PTR(proc->procs, Proc, childproc)
	{
		rc = generate_C_code(ravi_interface, childproc, mb);
		if (rc != 0)
			return rc;
	}
	END_FOR_EACH_PTR(childproc);
	return 0;
}

static inline AstNode *get_parent_function_of_upvalue(LuaSymbol *symbol)
{
	AstNode *upvalue_function = symbol->upvalue.target_function;
	AstNode *parent_function = upvalue_function->function_expr.parent_function;
	return parent_function;
}

/*
 * Returns an index for the up-value as required by Lua/Ravi runtime.
 * If the upvalue refers to a local variable in parent proto then idx should contain
 * the register for the local variable and instack should be true, else idx should have the index of
 * upvalue in parent proto and instack should be false.
 */
static unsigned get_upvalue_idx(Proc *proc, LuaSymbol *upvalue_symbol, bool *in_stack)
{
	*in_stack = false;
	LuaSymbol *underlying = upvalue_symbol->upvalue.target_variable;
	if (underlying->symbol_type == SYM_LOCAL) {
		/* Upvalue is in the stack of parent ? */
		AstNode *function_containing_local = underlying->variable.block->function;
		AstNode *parent_function = get_parent_function_of_upvalue(upvalue_symbol);
		if (parent_function == function_containing_local) {
			/* Upvalue is a local in parent function */
			*in_stack = true;
			return underlying->variable.pseudo->regnum;
		}
	}
	/* Search for the upvalue in parent function */
	LuaSymbol *sym;
	AstNode *this_function = upvalue_symbol->upvalue.target_function;
	FOR_EACH_PTR(this_function->function_expr.upvalues, LuaSymbol, sym)
	{
		if (sym->upvalue.target_variable == upvalue_symbol->upvalue.target_variable) {
			// Same variable
			return sym->upvalue.upvalue_index;
		}
	}
	END_FOR_EACH_PTR(sym);
	assert(0);
	return 0;
}

/**
 * Computes upvalue attributes needed by the Lua side
 */
static void compute_upvalue_attributes(Proc *proc)
{
	LuaSymbol *sym;
	AstNode *this_function = proc->function_expr;
	FOR_EACH_PTR(this_function->function_expr.upvalues, LuaSymbol, sym)
	{
		bool in_stack = false;
		unsigned idx = get_upvalue_idx(proc, sym, &in_stack);
		sym->upvalue.is_in_parent_stack = in_stack ? 1 : 0;
		sym->upvalue.parent_upvalue_index = idx; // TODO check overflow?
	}
	END_FOR_EACH_PTR(sym);
}

/*
 * Preprocess upvalues by populating a couple of attributes needed by the Lua side
 */
static void preprocess_upvalues(Proc *proc)
{
	compute_upvalue_attributes(proc);
	Proc *child_proc;
	FOR_EACH_PTR(proc->procs, Proc, child_proc) { preprocess_upvalues(child_proc); }
	END_FOR_EACH_PTR(childproc);
}

static void debug_message(void *context, const char *filename, long long line, const char *message)
{
	fprintf(stdout, "%s:%lld: %s\n", filename, line, message);
}
static void error_message(void *context, const char *message) { fprintf(stdout, "ERROR: %s\n", message); }

static struct Ravi_CompilerInterface stub_compilerInterface = {
    .context = NULL,
    .source = NULL,
    .source_len = 0,
    .source_name = "input",
    .compiler_options = "",
    .main_func_name = {"setup"},
    .generated_code = NULL,
    .debug_message = debug_message,
    .error_message = error_message,
};

/* Generate and compile C code */
int raviX_generate_C(LinearizerState *linearizer, TextBuffer *mb, struct Ravi_CompilerInterface *ravi_interface)
{
	if (ravi_interface == NULL)
		ravi_interface = &stub_compilerInterface;

	// _ENV is the name of the Lua up-value that points to the globals table
	raviX_create_string(linearizer->ast_container, "_ENV", 4);

	/* Add the common header portion */
	// FIXME we need a way to customise this for 32-bit vs 64-bit
	raviX_buffer_add_string(mb, Lua_header);

	/* Preprocess upvalue attributes */
	preprocess_upvalues(linearizer->main_proc);

	/* Recursively generate C code for procs */
	if (generate_C_code(ravi_interface, linearizer->main_proc, mb) != 0) {
		return -1;
	}
	generate_lua_closure(linearizer->main_proc, ravi_interface->main_func_name, mb);
	return 0;
}

void raviX_generate_C_tofile(LinearizerState *linearizer, const char *mainfunc, FILE *fp)
{
	struct Ravi_CompilerInterface *ravi_interface = &stub_compilerInterface;
	raviX_string_copy(ravi_interface->main_func_name, (mainfunc != NULL ? mainfunc : "setup"),
			  sizeof ravi_interface->main_func_name);
	TextBuffer mb;
	raviX_buffer_init(&mb, 4096);
	raviX_generate_C(linearizer, &mb, NULL);
	fprintf(fp, "%s\n", mb.buf);
	raviX_buffer_free(&mb);
}
