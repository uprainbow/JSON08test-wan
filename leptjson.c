#ifdef _WINDOWS
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <errno.h>   /* errno, ERANGE */
#include <math.h>    /* HUGE_VAL */
#include <stdio.h>   /* sprintf() */
#include <stdlib.h>  /* NULL, malloc(), realloc(), free(), strtod() */
#include <string.h>  /* memcpy() */

#ifndef LEPT_PARSE_STACK_INIT_SIZE  //使用 #ifndef X #define X ... #endif 方式的好处是，使用者可在编译选项中自行设置宏，没设置的话就用缺省值。
#define LEPT_PARSE_STACK_INIT_SIZE 256 //栈初始大小
#endif

#ifndef LEPT_PARSE_STRINGIFY_INIT_SIZE
#define LEPT_PARSE_STRINGIFY_INIT_SIZE 256  //生成器 临时缓冲区初始值大小
#endif

//实现JSON主要完成三个需求
//1.把 JSON 文本解析为一个树状数据结构（parse）
//2.提供接口访问该数据结构（access）
//3.把数据结构转换成 JSON 文本（stringify）


//先判断 预期值是否与实际值的第一个值相等  不等退  等 就将实际值的json指针前移一
#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0) 

#define ISDIGIT(ch)         ((ch) >= '0' && (ch) <= '9')
#define ISDIGIT1TO9(ch)     ((ch) >= '1' && (ch) <= '9')

//传入参数  lept_context 一个字符  目的是把这个字符添加到栈中
#define PUTC(c, ch)         do { *(char*)lept_context_push(c, sizeof(char)) = (ch); } while(0)

//传入参数  lept_context 一个字符串 字符串长度  目的  调用lept_context_push将字符串添加到栈中
#define PUTS(c, s, len)     memcpy(lept_context_push(c, len), s, len)


//首先为了减少解析函数之间传递多个参数，
//我们把这些数据都放进一个 lept_context 结构体：
typedef struct {

	const char* json;//存放要解析的文本内容

	char* stack; //临时缓冲区  利用堆栈动态数组的数据结构 空间不足时自动扩展
	size_t size, top;//由于我们会扩展空间大小  如果使用指针存储top会失效   所以用下标的方式存储top

}lept_context;


//压栈操作  传入  栈  压入数据的大小  返回压入数据的首地址
static void* lept_context_push(lept_context* c, size_t size) {
	void* ret;
	assert(size > 0);

	//判断压入数据后大小是否超出栈的大小
	if (c->top + size >= c->size) {
		//超出了

		//初始值
		if (c->size == 0)
			c->size = LEPT_PARSE_STACK_INIT_SIZE;

		//循环每次增加到1.5倍 直到足够大
		while (c->top + size >= c->size)
			c->size += c->size >> 1;  /* c->size * 1.5 */

		//重新定义stack的空间大小 并复制栈中已经有的数据
		c->stack = (char*)realloc(c->stack, c->size);
	}

	//ret记录头指针
	ret = c->stack + c->top;

	//重新定义top的大小
	c->top += size;

	return ret;
}

//出栈  更新top的值  返回要出栈的数据的首地址
static void* lept_context_pop(lept_context* c, size_t size) {
	assert(c->top >= size);
	return c->stack + (c->top -= size);
}


//空格跳过函数
static void lept_parse_whitespace(lept_context* c)
{
	const char *p = c->json;
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	c->json = p;
}


//解析  null  true  false  的函数
static int lept_parse_literal(lept_context* c, lept_value* v, const char* literal, lept_type type)
{
	size_t i;
	EXPECT(c, literal[0]);

	for (i = 0; literal[i + 1]; i++)
	{
		if (c->json[i] != literal[i + 1])
			return LEPT_PARSE_INVALID_VALUE;
	}

	//解析成功   是null false true中的一个
	c->json += i;//指针移动到下个值的首地址
	v->type = type;//类型改为预期值类型

	return LEPT_PARSE_OK;
}


//解析number
static int lept_parse_number(lept_context* c, lept_value* v) {

	/*   JOSN number类型
	number = [ "-" ] int [ frac ] [ exp ]
	int = "0" / digit1-9 *digit
	frac = "." 1*digit
	exp = ("e" / "E") ["-" / "+"] 1*digit
	*/

	const char* p = c->json;
	if (*p == '-') p++;
	if (*p == '0') p++;
	else {
		//符号后不能只有0
		if (!ISDIGIT1TO9(*p))
			return LEPT_PARSE_INVALID_VALUE;

		for (p++; ISDIGIT(*p); p++);
	}
	if (*p == '.') {
		p++;
		//点后边没数
		if (!ISDIGIT(*p))
			return LEPT_PARSE_INVALID_VALUE;

		for (p++; ISDIGIT(*p); p++);
	}
	if (*p == 'e' || *p == 'E') {
		p++;
		if (*p == '+' || *p == '-') p++;
		if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
		for (p++; ISDIGIT(*p); p++);
	}

	//解析number成功
	errno = 0;

	//strtod() 可转换 JSON 所要求的格式  把十进制的数字转换成二进制的 double
	//解析不成功   
	v->u.n = strtod(c->json, NULL);

	// 如果errno=ERANGE 说明范围错误数字过大
	if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
		return LEPT_PARSE_NUMBER_TOO_BIG;

	//数据类型变  指针位置变
	v->type = LEPT_NUMBER;
	c->json = p;

	return LEPT_PARSE_OK;
}

//检验  /u后是否时4位有效数字   传入json 和一个存放解析成功转化的十进制数
static const char* lept_parse_hex4(const char* p, unsigned* u) {
	int i;
	*u = 0;
	for (i = 0; i < 4; i++) {

		char ch = *p++;

		*u <<= 4;//每次将数扩大2的4次方  相当于解析对应十六进制的一个字符

		//将每个字符转化位对应的十进制数
		if (ch >= '0' && ch <= '9')  *u |= ch - '0';
		else if (ch >= 'A' && ch <= 'F')  *u |= ch - ('A' - 10);
		else if (ch >= 'a' && ch <= 'f')  *u |= ch - ('a' - 10);

		//字符无效转化失败
		else return NULL;
	}
	//解析成功
	return p;
}

//解析UTF-8编码格式  传入lept_context 和 解析Unicode所得的十进制数
static void lept_encode_utf8(lept_context* c, unsigned u) {
	//char，为什么要做 x & 0xFF 这种操作呢？
	//这是因为 u 是 unsigned 类型，
	//一些编译器可能会警告这个转型可能会截断数据。
	//但实际上，配合了范围的检测然后右移之后，
	//可以保证写入的是 0~255 内的值。
	//为了避免一些编译器的警告误判，
	//我们加上 u & 0xFF


	//码点范围：U+0000-U+007F	 码点位数：7	  字节1：0xxxxxxx	
	if (u <= 0x7F)
		PUTC(c, u & 0xFF);

	//码点范围：U+0080 ~ U+07FF	   码点位数：11	    字节1：110xxxxx 字节2：10xxxxxx	
	else if (u <= 0x7FF) {
		PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
		PUTC(c, 0x80 | (u & 0x3F));
	}

	//码点范围：U+0800 ~ U+FFFF	 码点位数：16	  字节1：110xxxxx 字节2：10xxxxxx  字节3：10xxxxxx
	else if (u <= 0xFFFF) {
		PUTC(c, 0xE0 | ((u >> 12) & 0xFF));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}

	//码点范围：U+10000 ~ U+10FFFF	 码点位数：21	  字节1：11110xxx  字节2：10xxxxxx  字节3：10xxxxxx	 字节4：10xxxxxx
	else {
		assert(u <= 0x10FFFF);
		PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
		PUTC(c, 0x80 | ((u >> 12) & 0x3F));
		PUTC(c, 0x80 | ((u >> 6) & 0x3F));
		PUTC(c, 0x80 | (u & 0x3F));
	}
}

//传入解析结果   更新top直接跳过此字符   重构返回错误吗的处理抽取为宏
#define STRING_ERROR(ret) do { c->top = head; return ret; } while(0)

//解析 JSON 字符串，把结果写入 str 和 len  str 指向 c->stack 中的元素，需要在 c->stack
//这样的话，我们实现对象的解析时，就可以使用 lept_parse_string_raw()　来解析 JSON 字符串，然后把结果复制至 lept_member 的 k 和 klen 字段。
static int lept_parse_string_raw(lept_context* c, char** str, size_t* len) {

	size_t head = c->top;//标记此时还没push栈的top   用于解析过程中失败从而还原top值
	unsigned u, u2;
	const char* p;

	EXPECT(c, '\"');//跳过 "
	p = c->json;

	for (;;) {
		char ch = *p++;
		switch (ch) {

			//表示字符串的结束
		case '\"':
			*len = c->top - head;//字符串的长度
			*str = lept_context_pop(c, *len);//出栈后的头指针
			c->json = p;//移动文本指针
			return LEPT_PARSE_OK;//成功

		//转移字符
		case '\\':
			switch (*p++) {
			case '\"': PUTC(c, '\"'); break;
			case '\\': PUTC(c, '\\'); break;
			case '/':  PUTC(c, '/'); break;
			case 'b':  PUTC(c, '\b'); break;
			case 'f':  PUTC(c, '\f'); break;
			case 'n':  PUTC(c, '\n'); break;
			case 'r':  PUTC(c, '\r'); break;
			case 't':  PUTC(c, '\t'); break;

				//Unicode码转十进制再转UTF-8编码格式
			case 'u':

				//先将十六进制转化位十进制数存入u中
				if (!(p = lept_parse_hex4(p, &u)))
					STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);

				//U+0000 至 U+FFFF 这组 Unicode 字符称为基本多文种平面（basic multilingual plane, BMP），
				//还有另外 16 个平面。
				//那么 BMP 以外的字符，JSON 会使用代理对（surrogate pair）表示 \uXXXX\uYYYY。
				//在 BMP 中，保留了 2048 个代理码点。

				//如果第一个码点是U+D800 至 U+DBFF  它的代码对的高代理项 （high surrogate）
				if (u >= 0xD800 && u <= 0xDBFF) { /* surrogate pair */

					//之后应该伴随一个 U+DC00 至 U+DFFF 的低代理项（low surrogate）
					if (*p++ != '\\')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
					if (*p++ != 'u')
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);

					//再次解析一个 \u后是否是4位十六进位数字
					if (!(p = lept_parse_hex4(p, &u2)))
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);

					//U+DC00 至 U+DFFF 的低代理项
					if (u2 < 0xDC00 || u2 > 0xDFFF)
						STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);

					//我们用下列公式把代理对 (H, L) 变换成真实的码点：
					//codepoint = 0x10000 + (H − 0xD800) × 0x400 + (L − 0xDC00)
					u = (((u - 0xD800) << 10) | (u2 - 0xDC00)) + 0x10000;

				}

				//转化为UTF-8编码格式
				lept_encode_utf8(c, u);
				break;
			default:
				//转移无效  更新top 跳过字符串
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
			}
			break;

			//在 C 语言中，字符串一般表示为空结尾字符串（null-terminated string），即以空字符（'\0'）代表字符串的结束。
			//然而，JSON 字符串是允许含有空字符的，
			//例如这个 JSON "Hello\u0000World" 就是单个字符串，解析后为11个字符。
			//如果纯粹使用空结尾字符串来表示 JSON 解析后的结果，就没法处理空字符。
			//因此，我们可以分配内存来储存解析后的字符，以及记录字符的数目（即字符串长度）。
			//由于大部分 C 程序都假设字符串是空结尾字符串，我们还是在最后加上一个空字符，
			//那么不需处理 \u0000 这种字符的应用可以简单地把它当作是空结尾字符串。
		case '\0':   //错误
			STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);

		default:
			//unescaped = %x20-21 / %x23-5B / %x5D-10FFFF  合法的字符值
			//当中空缺的 %x22 是双引号，%x5C 是反斜线，都已经处理
			//所以不合法的字符是 %x00 至 %x1F
			if ((unsigned char)ch < 0x20)
				//不合法字符值
				STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
			PUTC(c, ch);//push进栈 等到解析成功后出栈
		}
	}
}

//解析字符串
static int lept_parse_string(lept_context* c, lept_value* v) {
	int ret;
	char* s;
	size_t len;

	//把解析 JSON 字符串及写入 lept_value 分拆成两部分   此处代码重构是方便于对象键值string的解析
	if ((ret = lept_parse_string_raw(c, &s, &len)) == LEPT_PARSE_OK)
		lept_set_string(v, s, len);
	return ret;
}

static int lept_parse_value(lept_context* c, lept_value* v);//前向声明

//解析数组
static int lept_parse_array(lept_context* c, lept_value* v) {

	//JOSN数组语法  array = %x5B ws [ value *( ws %x2C ws value ) ] ws %x5D
	//%x5B 是左中括号 [，%x2C 是逗号 ,，%x5D 是右中括号 ] 
	size_t i, size = 0;
	int ret;

	EXPECT(c, '[');

	lept_parse_whitespace(c);

	//我们只需要把每个解析好的元素压入堆栈，解析到数组结束时，
	//再一次性把所有元素弹出，复制至新分配的内存之中
	if (*c->json == ']') {

		//解析成功
		c->json++;
		lept_set_array(v, 0);//数组为空

		return LEPT_PARSE_OK;
	}
	for (;;) {

		lept_value e;//临时lept_value 用于存储之后的元素
		lept_init(&e);

		if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK)
			//解析失败
			break;

		//解析成功数组中的一个值放入e中   分配数据空间  将 e复制到栈中 完成数据入栈
		memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
		size++;

		lept_parse_whitespace(c);

		//JOSN数组语法  array = %x5B ws [ value *( ws %x2C ws value ) ] ws %x5D

		if (*c->json == ',') {//有另一个值
			c->json++;
			lept_parse_whitespace(c);
		}
		else if (*c->json == ']') {//没有另一个值
			//解析成功
			c->json++;
			//分配size个lept_value空间
			lept_set_array(v, size);
			//将栈中所有的值复制到最终的e中
			memcpy(v->u.a.e, lept_context_pop(c, size * sizeof(lept_value)), size * sizeof(lept_value));
			//更新size
			v->u.a.size = size;

			return LEPT_PARSE_OK;
		}
		else {
			//失败
			ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
			break;
		}
	}
	/* Pop and free values on the stack */
	for (i = 0; i < size; i++)
		//将栈中开辟的所有值free   因为每次pop函数都会返回一个值的首地址更新top   所以free  size次就可以
		lept_free((lept_value*)lept_context_pop(c, sizeof(lept_value)));
	return ret;
}

//解析对象
static int lept_parse_object(lept_context* c, lept_value* v) {
	//JSON对象的语法
	//member = string ws %x3A ws value
	//object = %x7B ws[member *(ws %x2C ws member)] ws %x7D
	//花括号 {}（U+007B、U+007D） 
	//键值对，键必须为 JSON 字符串，然后值是任何 JSON 值中间以冒号 :（U+003A）分隔

	size_t i, size;
	lept_member m;
	int ret;

	EXPECT(c, '{');

	lept_parse_whitespace(c);

	if (*c->json == '}') {
		//解析成功
		c->json++;
		lept_set_object(v, 0);//空对象

		return LEPT_PARSE_OK;
	}

	m.k = NULL;
	size = 0;

	for (;;) {
		char* str;
		lept_init(&m.v);

		/* parse key 键*/
		if (*c->json != '"') {
			ret = LEPT_PARSE_MISS_KEY;
			break;
		}

		//解析键值string
		if ((ret = lept_parse_string_raw(c, &str, &m.klen)) != LEPT_PARSE_OK)
			break;
		memcpy(m.k = (char*)malloc(m.klen + 1), str, m.klen);//临时mmber分配内存
		m.k[m.klen] = '\0';//记得封死字符指针

		/* parse ws colon ws */
		lept_parse_whitespace(c);

		//member = string ws %x3A ws value
		if (*c->json != ':') {
			ret = LEPT_PARSE_MISS_COLON;
			break;
		}
		c->json++;

		lept_parse_whitespace(c);

		/* parse value  值*/
		if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK)
			break;

		//解析成功  入栈
		memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(lept_member));
		size++;
		m.k = NULL; /* ownership is transferred to member on stack 所有权转移到栈*/

		////object = %x7B ws[member *(ws %x2C ws member)] ws %x7D

		/* parse ws [comma | right-curly-brace] ws */
		lept_parse_whitespace(c);

		if (*c->json == ',') {//有其他值
			//递归调用 lept_parse_value()，
			//把结果写入临时 lept_member 的 v 字段，
			//然后把整个 lept_member 压入栈：   与数组相似
			c->json++;
			lept_parse_whitespace(c);
		}
		else if (*c->json == '}') {//没有其他值
			c->json++;
			lept_set_object(v, size);
			//出栈并复制到最终v
			memcpy(v->u.o.m, lept_context_pop(c, sizeof(lept_member) * size), sizeof(lept_member) * size);
			v->u.o.size = size;
			//解析成功
			return LEPT_PARSE_OK;
		}
		else {//解析失败   即没逗号也没花括号
			ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
			break;
		}
	}

	/* Pop and free members on the stack */
	free(m.k);//释放临时字符串
	for (i = 0; i < size; i++) {

		//释放存放在栈上的成员空间
		lept_member* m = (lept_member*)lept_context_pop(c, sizeof(lept_member));
		free(m->k);
		lept_free(&m->v);
	}
	v->type = LEPT_NULL;

	//而v的内存则是在测试完成时释放
	return ret;
}

//解析函数接口 返回解析结果
static int lept_parse_value(lept_context* c, lept_value* v) {
	switch (*c->json) {

		//由于解析 t  f   n代码过程相似   为了节约代码空间   合并成一个解析函数
	case 't':  return lept_parse_literal(c, v, "true", LEPT_TRUE);
	case 'f':  return lept_parse_literal(c, v, "false", LEPT_FALSE);
	case 'n':  return lept_parse_literal(c, v, "null", LEPT_NULL);

	case '"':  return lept_parse_string(c, v);

	case '[':  return lept_parse_array(c, v);

	case '{':  return lept_parse_object(c, v);

	case '\0': return LEPT_PARSE_EXPECT_VALUE;

	default:   return lept_parse_number(c, v);
	}
}


//API函数     解析JSON函数
int lept_parse(lept_value* v, const char* json) {

	//JSON - text = ws value ws

	lept_context c;
	int ret;
	assert(v != NULL);

	//初始化stack   并最终释放内存
	c.json = json;
	c.stack = NULL;
	c.size = c.top = 0;
	lept_init(v);

	//第一个w
	lept_parse_whitespace(&c);

	//value
	//ret来记录解析结果  如果解析成功就继续
	if ((ret = lept_parse_value(&c, v)) == LEPT_PARSE_OK)
	{

		//第二个w
		lept_parse_whitespace(&c);

		//如果第二个w后边还有数   说明不满足JSON语法  
		if (*c.json != '\0')
		{
			//将数据结果 和 解析结果=null  变为对应的类型
			v->type = LEPT_NULL;
			ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
		}

	}

	//释放stack的内存
	assert(c.top == 0);//加入断言确保所有数据都被弹出
	free(c.stack);

	return ret;
}

//字符串生成器 传入lept_context 字符串   字符串长度
static void lept_stringify_string(lept_context* c, const char* s, size_t len) {
	static const char hex_digits[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	size_t i, size;
	char* head, *p;
	assert(s != NULL);

	//创建栈空间   入栈  此地做了优化   就是直接分配足够大的栈空间    直接添加进去   最后调整top的大小
	p = head = lept_context_push(c, size = len * 6 + 2); /* "\u00xx..." */

	*p++ = '"';
	for (i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)s[i];

		switch (ch) {
		case '\"': *p++ = '\\'; *p++ = '\"'; break;
		case '\\': *p++ = '\\'; *p++ = '\\'; break;
		case '\b': *p++ = '\\'; *p++ = 'b';  break;
		case '\f': *p++ = '\\'; *p++ = 'f';  break;
		case '\n': *p++ = '\\'; *p++ = 'n';  break;
		case '\r': *p++ = '\\'; *p++ = 'r';  break;
		case '\t': *p++ = '\\'; *p++ = 't';  break;
		default:
			if (ch < 0x20) {
				*p++ = '\\'; *p++ = 'u'; *p++ = '0'; *p++ = '0';
				*p++ = hex_digits[ch >> 4];	//十六进制的右边第二个字符  向右移4位得到对应的十进制
				*p++ = hex_digits[ch & 15];  // 15： 0000 0000 0000 1111
			}
			else
				*p++ = s[i];
		}
	}
	*p++ = '"';
	//开始top为加入的最大值  更新top为实际值
	c->top -= size - (p - head);
}

//生成器生成功能    传入 临时缓冲区栈  和  解析的值     目的是将解析的值转化为字符串放入栈中
static void lept_stringify_value(lept_context* c, const lept_value* v) {
	size_t i;
	switch (v->type) {

	case LEPT_NULL:   PUTS(c, "null", 4); break;
	case LEPT_FALSE:  PUTS(c, "false", 5); break;
	case LEPT_TRUE:   PUTS(c, "true", 4); break;

		//数字   
		//为了简单起见，我们使用 sprintf("%.17g", ...) 来把浮点数转换成文本。
		//"%.17g" 是足够把双精度浮点转换成可还原的文本。
	case LEPT_NUMBER: c->top -= 32 - sprintf(lept_context_push(c, 32), "%.17g", v->u.n); break;

		//字符串
	case LEPT_STRING: lept_stringify_string(c, v->u.s.s, v->u.s.len); break;

		//数组
	case LEPT_ARRAY:
		PUTC(c, '[');
		for (i = 0; i < v->u.a.size; i++) {
			if (i > 0)
				PUTC(c, ',');
			//递归生成
			lept_stringify_value(c, &v->u.a.e[i]);
		}
		PUTC(c, ']');
		break;

		//对象
	case LEPT_OBJECT:
		PUTC(c, '{');
		for (i = 0; i < v->u.o.size; i++) {
			if (i > 0)
				PUTC(c, ',');

			//将键生成并添加到栈中
			lept_stringify_string(c, v->u.o.m[i].k, v->u.o.m[i].klen);

			PUTC(c, ':');

			//递归生成
			lept_stringify_value(c, &v->u.o.m[i].v);
		}
		PUTC(c, '}');
		break;

		//非法类型
	default: assert(0 && "invalid type");
	}
}

//生成器    传入lept_value 和 JSON的长度 传入NULL可忽略此参数   返回栈的首地址
char* lept_stringify(const lept_value* v, size_t* length) {

	lept_context c;//做动态数组

	assert(v != NULL);

	//创建栈空间  
	c.stack = (char*)malloc(c.size = LEPT_PARSE_STRINGIFY_INIT_SIZE);
	c.top = 0;

	//生成字符串
	lept_stringify_value(&c, v);

	if (length)
		*length = c.top;//更新生成后字符的长度
	PUTC(&c, '\0');//封

	//栈和v  的内存都在test里释放
	return c.stack;
}

//复制功能   传入两个lept_value
void lept_copy(lept_value* dst, const lept_value* src) {
	assert(src != NULL && dst != NULL && src != dst);
	switch (src->type) {
		size_t i;
		//set的空间   会在test中free

		//字符串
	case LEPT_STRING:
		lept_set_string(dst, src->u.s.s, src->u.s.len);
		break;

		//数组
	case LEPT_ARRAY:
		/* \todo */
		lept_set_array(dst, src->u.a.capacity);
		dst->u.a.size = src->u.a.size;

		//递归进行数组各个值的复制
		for (i = 0; i < src->u.a.size; i++)
		{
			lept_copy(&dst->u.a.e[i], &src->u.a.e[i]);
		}
		break;

		//对象
	case LEPT_OBJECT:
		/* \todo */
		lept_set_object(dst, src->u.o.capacity);
		for (i = 0; i < src->u.o.size; i++)
		{
			//创建键值对并对键复制  递归复制值
			lept_copy(lept_set_object_value(dst, src->u.o.m[i].k, src->u.o.m->klen), &src->u.o.m[i].v);
		}
		dst->u.o.size = src->u.o.size;
		break;

		//true false null 数字   直接复制type  n
	default:
		lept_free(dst);
		memcpy(dst, src, sizeof(lept_value));
		break;
	}
}

//移动功能
void lept_move(lept_value* dst, lept_value* src) {
	assert(dst != NULL && src != NULL && src != dst);
	lept_free(dst);
	memcpy(dst, src, sizeof(lept_value));
	lept_init(src);
}

//交换功能
void lept_swap(lept_value* lhs, lept_value* rhs) {
	assert(lhs != NULL && rhs != NULL);
	if (lhs != rhs) {
		lept_value temp;
		memcpy(&temp, lhs, sizeof(lept_value));
		memcpy(lhs, rhs, sizeof(lept_value));
		memcpy(rhs, &temp, sizeof(lept_value));
	}
}

//释放lept_value的内存
void lept_free(lept_value* v) {
	size_t i;
	assert(v != NULL);
	switch (v->type) {

		//string
	case LEPT_STRING:
		free(v->u.s.s);
		break;

		//数组
	case LEPT_ARRAY:
		for (i = 0; i < v->u.a.size; i++)
			lept_free(&v->u.a.e[i]);
		free(v->u.a.e);
		break;

		//对象
	case LEPT_OBJECT:
		for (i = 0; i < v->u.o.size; i++) {
			free(v->u.o.m[i].k);
			lept_free(&v->u.o.m[i].v);
		}
		free(v->u.o.m);
		break;

		//不用free
	default: break;
	}
	v->type = LEPT_NULL;
}

//获取结点的数据类型
lept_type lept_get_type(const lept_value* v)
{
	assert(v != NULL);
	return v->type;
}

//比较两个lept_vlaue  是否相等
int lept_is_equal(const lept_value* lhs, const lept_value* rhs) {
	//对于 true、false、null 这三种类型，比较类型后便完成比较
	size_t i;
	assert(lhs != NULL && rhs != NULL);

	if (lhs->type != rhs->type)
		return 0;

	switch (lhs->type) {

		//字符串
	case LEPT_STRING:
		return lhs->u.s.len == rhs->u.s.len &&
			memcmp(lhs->u.s.s, rhs->u.s.s, lhs->u.s.len) == 0;

		//数字
	case LEPT_NUMBER:
		return lhs->u.n == rhs->u.n;

		//数组
	case LEPT_ARRAY:
		if (lhs->u.a.size != rhs->u.a.size)
			return 0;
		for (i = 0; i < lhs->u.a.size; i++)
			if (!lept_is_equal(&lhs->u.a.e[i], &rhs->u.a.e[i]))
				return 0;
		return 1;

		//对象
	case LEPT_OBJECT:
		/* \todo */
		//概念上对象的键值对是无序的  所以可以简单地利用 lept_find_object_index() 去找出对应的值  然后递归作比较
		if (lhs->u.o.size != rhs->u.a.size)
			return 0;
		for (i = 0; i < rhs->u.o.size; i++)
		{
			size_t index = lept_find_object_index(lhs, rhs->u.o.m[i].k, rhs->u.o.m[i].klen);
			if (index == LEPT_KEY_NOT_EXIST)
				return 0;
			//rhs中的键值  lhs中存在  再递归检擦其值是否相等
			if (!lept_is_equal(&lhs->u.o.m[index].v, &rhs->u.o.m[i].v))
				return 0;
		}
		return 1;
		//非法类型
	default:
		return 1;
	}
}

//读出true 或 false
int lept_get_boolean(const lept_value* v) {
	assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
	return v->type == LEPT_TRUE;
}

//写入true 或  false
void lept_set_boolean(lept_value* v, int b) {
	lept_free(v);//先将值free  再重新定义数据类型
	v->type = b ? LEPT_TRUE : LEPT_FALSE;
}

//读出数字
double lept_get_number(const lept_value* v) {
	//检验数据类型是否为LEPT_NUMBER类型  确保类型的正确
	assert(v != NULL && v->type == LEPT_NUMBER);
	return v->u.n;
}

//写入数字
void lept_set_number(lept_value* v, double n) {
	lept_free(v);
	v->u.n = n;
	v->type = LEPT_NUMBER;
}

//读出字符
const char* lept_get_string(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.s;
}

//读出字符长度
size_t lept_get_string_length(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_STRING);
	return v->u.s.len;
}

//读入字符串  string空间分配函数   传入  结点  字符串  字符串长度
void lept_set_string(lept_value* v, const char* s, size_t len) {
	assert(v != NULL && (s != NULL || len == 0));

	//在设置这个 v 之前，我们需要先调用 lept_free(v) 去清空 v 可能分配到的内存 因为可能原就有字符串
	lept_free(v);

	v->u.s.s = (char*)malloc(len + 1);

	memcpy(v->u.s.s, s, len);

	//封 长度 类型
	v->u.s.s[len] = '\0';
	v->u.s.len = len;
	v->type = LEPT_STRING;
}

//创建数组空间  更新capacity
void lept_set_array(lept_value* v, size_t capacity) {
	assert(v != NULL);
	lept_free(v);
	v->type = LEPT_ARRAY;
	v->u.a.size = 0;
	v->u.a.capacity = capacity;
	v->u.a.e = capacity > 0 ? (lept_value*)malloc(capacity * sizeof(lept_value)) : NULL;
}

//得到数组的元素个数
size_t lept_get_array_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->u.a.size;
}

//得到数组的容量
size_t lept_get_array_capacity(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	return v->u.a.capacity;
}

//重新定义array的空间
void lept_reserve_array(lept_value* v, size_t capacity) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.capacity < capacity) {
		v->u.a.capacity = capacity;
		v->u.a.e = (lept_value*)realloc(v->u.a.e, capacity * sizeof(lept_value));
	}
}

//
void lept_shrink_array(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.capacity > v->u.a.size) {
		v->u.a.capacity = v->u.a.size;
		v->u.a.e = (lept_value*)realloc(v->u.a.e, v->u.a.capacity * sizeof(lept_value));
	}
}

void lept_clear_array(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	lept_erase_array_element(v, 0, v->u.a.size);
}

//得到传入数组下标的元素  返回类型  lept_value*
lept_value* lept_get_array_element(lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	assert(index < v->u.a.size);
	return &v->u.a.e[index];
}

lept_value* lept_pushback_array_element(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY);
	if (v->u.a.size == v->u.a.capacity)
		lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
	lept_init(&v->u.a.e[v->u.a.size]);
	return &v->u.a.e[v->u.a.size++];
}

void lept_popback_array_element(lept_value* v) {
	assert(v != NULL && v->type == LEPT_ARRAY && v->u.a.size > 0);
	lept_free(&v->u.a.e[--v->u.a.size]);
}

lept_value* lept_insert_array_element(lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_ARRAY && index <= v->u.a.size);
	/* \todo */
	//首先确定array大小适否
	if (v->u.a.size == v->u.a.capacity)
		lept_reserve_array(v, v->u.a.capacity == 0 ? 1 : v->u.a.capacity * 2);
	//向后复制空出一个位置
	memcpy(v->u.a.e + index + 1, v->u.a.e + index, (v->u.a.size - index) * sizeof(lept_value));
	//更新空出位置的类型   不能free  此时只是改变了值  并没有销毁值
	lept_init(&v->u.a.e[index]);
	//更新size
	v->u.a.size++;
	return &v->u.a.e[index];
}

// 删去在 index 位置开始共 count 个元素（不改容量）
void lept_erase_array_element(lept_value* v, size_t index, size_t count) {
	assert(v != NULL && v->type == LEPT_ARRAY && index + count <= v->u.a.size);
	/* \todo */
	if (count == 0)
		return;
	size_t i, j;
	//首先free要删除的值
	for (i = index; i < index + count; i++)
		lept_free(&v->u.a.e[i]);
	//将后边的复制到删除的空位置
	memcpy(v->u.a.e + index, v->u.a.e + index + count, (v->u.a.size - index - count) * sizeof(lept_value));
	//更新复制过去的值类型
	for (i = 1, j = v->u.a.size - 1; i <= count; i++, j--)
		lept_init(&v->u.a.e[j]);
	//更新size
	v->u.a.size -= count;
}

//创建对象objec空间  传入lept_value  capacity   更新capacity
void lept_set_object(lept_value* v, size_t capacity) {
	assert(v != NULL);
	lept_free(v);
	v->type = LEPT_OBJECT;
	v->u.o.size = 0;
	v->u.o.capacity = capacity;
	v->u.o.m = capacity > 0 ? (lept_member*)malloc(capacity * sizeof(lept_member)) : NULL;
}

//得到对象中键值对的数量
size_t lept_get_object_size(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	return v->u.o.size;
}

//得到对象的容量
size_t lept_get_object_capacity(const lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	/* \todo */
	return v->u.o.capacity;
}

//检测对象的容量   传入lept_value  和期望值
void lept_reserve_object(lept_value* v, size_t capacity) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	/* \todo */
	if (v->u.o.capacity < capacity) {
		v->u.o.capacity = capacity;
		v->u.o.m = (lept_member*)realloc(v->u.o.m, capacity * sizeof(lept_member));
	}
}

void lept_shrink_object(lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	/* \todo */
	if (v->u.o.capacity > v->u.o.size) {
		v->u.o.capacity = v->u.o.size;
		v->u.o.m = (lept_member*)realloc(v->u.o.m, v->u.o.capacity * sizeof(lept_member));
	}
}

//清除所有元素（不改容量）
void lept_clear_object(lept_value* v) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	/* \todo */
	size_t i;
	for (i = 0; i < v->u.o.size; i++)
	{
		free(v->u.o.m[i].k);
		v->u.o.m->klen = 0;
		lept_free(&v->u.o.m->v);
	}
	v->u.o.size = 0;
}

//得到对象对应下标的键值  返回字符指针k
const char* lept_get_object_key(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].k;
}

//得到对象对应下标的键值的长度
size_t lept_get_object_key_length(const lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return v->u.o.m[index].klen;
}

//得到对象对应下标的值  返回lept_value
lept_value* lept_get_object_value(lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT);
	assert(index < v->u.o.size);
	return &v->u.o.m[index].v;
}

//查询一个键值是否存在    传入lept_value  要查询的键值key   键值的长度klen   返回键值的index
size_t lept_find_object_index(const lept_value* v, const char* key, size_t klen) {
	size_t i;
	assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);

	//线性查找
	for (i = 0; i < v->u.o.size; i++)
		if (v->u.o.m[i].klen == klen && memcmp(v->u.o.m[i].k, key, klen) == 0)
			return i;

	//查找失败 返回-1
	return LEPT_KEY_NOT_EXIST;
}

//查找对象的值  传入lept_value 键值 键值长度  返回键值对应的值 lept_value
lept_value* lept_find_object_value(lept_value* v, const char* key, size_t klen) {
	size_t index = lept_find_object_index(v, key, klen);
	return index != LEPT_KEY_NOT_EXIST ? &v->u.o.m[index].v : NULL;
}

//创建键值对空间  传入lept_value  key键  键长度   返回新增键值对的值指针
lept_value* lept_set_object_value(lept_value* v, const char* key, size_t klen) {
	assert(v != NULL && v->type == LEPT_OBJECT && key != NULL);
	/* \todo */
	//对应键值已经存在  直接返回值的指针
	size_t index = lept_find_object_index(v, key, klen);
	if (index != LEPT_KEY_NOT_EXIST)
		return &v->u.o.m[index].v;
	//添加键值对  首先确定object的容量适否
	size_t tem = v->u.o.size;
	if (v->u.o.size == v->u.o.capacity) {
		lept_reserve_object(v, v->u.o.capacity == 0 ? 1 : (v->u.o.capacity << 1));
	}
	v->u.o.m[tem].k = (char *)malloc(klen + 1);
	memcpy(v->u.o.m[v->u.o.size].k, key, klen);
	v->u.o.m[tem].k[klen] = '\0';
	v->u.o.m[tem].klen = klen;
	lept_init(&v->u.o.m[tem].v);
	//更新size
	v->u.o.size++;
	return &v->u.o.m[tem].v;
}

//删除给定位置的值
void lept_remove_object_value(lept_value* v, size_t index) {
	assert(v != NULL && v->type == LEPT_OBJECT && index < v->u.o.size);
	/* \todo */
	free(v->u.o.m[index].k);
	lept_free(&v->u.o.m[index].v);
	memcpy(v->u.o.m + index, v->u.o.m + index + 1, (v->u.o.size - 1 - index) * sizeof(lept_member));
	--v->u.o.size;
	v->u.o.m[v->u.o.size].k = NULL;
	v->u.o.m[v->u.o.size].klen = 0;
	lept_init(&v->u.o.m[v->u.o.size].v);
}

/*         JSON语法子集   使用 RFC7159 中的 ABNF 表示：
JSON - text = ws value ws
ws = *(%x20 / %x09 / %x0A / %x0D)
value = null / false / true
null = "null"
false = "false"
true = "true"
*/

/*
宏里有多过一个语句（statement），就需要用 do {    } while (0) 包裹成单个语句
*/

/*
断言（assertion）是 C 语言中常用的防御式编程方式，减少编程错误。
最常用的是在函数开始的地方，检测所有参数。
有时候也可以在调用函数后，检查上下文是否正确。
C 语言的标准库含有 assert() 这个宏（需 #include <assert.h>），提供断言功能。
当程序以 release 配置编译时（定义了 NDEBUG 宏），assert() 不会做检测；
而当在 debug 配置时（没定义 NDEBUG 宏），
则会在运行时检测 assert(cond) 中的条件是否为真（非 0），
断言失败会直接令程序崩溃。
如果那个错误是由于程序员错误编码所造成的（例如传入不合法的参数），那么应用断言
*/

/*   JOSN number类型
number = [ "-" ] int [ frac ] [ exp ]
int = "0" / digit1-9 *digit
frac = "." 1*digit
exp = ("e" / "E") ["-" / "+"] 1*digit
*/

/*
JSON共支持9种转义序列
string = quotation-mark *char quotation-mark
char = unescaped /
   escape (
	   %x22 /          ; "    quotation mark  U+0022
	   %x5C /          ; \    reverse solidus U+005C
	   %x2F /          ; /    solidus         U+002F
	   %x62 /          ; b    backspace       U+0008
	   %x66 /          ; f    form feed       U+000C
	   %x6E /          ; n    line feed       U+000A
	   %x72 /          ; r    carriage return U+000D
	   %x74 /          ; t    tab             U+0009
	   %x75 4HEXDIG )  ; uXXXX                U+XXXX
	   escape = %x5C   ; \
quotation-mark = %x22  ; "
unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
*/