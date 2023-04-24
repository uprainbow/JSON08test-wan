#ifndef LEPTJSON_H__
#define LEPTJSON_H__

#include <stddef.h> /* size_t */

//表示 6种数据结构  null  (bool)true/false  number(一般的浮点数表示方式)  string  object(对象  键值对)
typedef enum
{
	LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPT_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT
} lept_type;

//键值不存在  返回-1
#define LEPT_KEY_NOT_EXIST ((size_t)-1)

//用于在lept_value中使用lept_value/lept_member  的前向声明
typedef struct lept_value lept_value;
typedef struct lept_member lept_member;

//JOSN的数据结构
struct lept_value
{
	union {//一个值只能有一个数据类型 所以用union来节省内存

		struct { lept_member* m; size_t size, capacity; }o; /* object: members, member count, capacity */

		struct { lept_value*  e; size_t size, capacity; }a; /* array:  elements, element count, capacity */

		struct { char* s; size_t len; }s;                   /* string: null-terminated string, string length */

		double n;                                           /* number */
	}u;
	lept_type type; //表示该结点属于哪种数据结构类型
};

//对象中键值对的数据结构
struct lept_member
{
	char* k; size_t klen;   /* member key string, key string length */
	lept_value v;           /* member value */
};


enum   //调用解析函数  返回的解析的结果
{
	LEPT_PARSE_OK = 0,//表示解析成功
	LEPT_PARSE_EXPECT_VALUE,//若一个 JSON 只含有空白			
	LEPT_PARSE_INVALID_VALUE,//若值不是那三种字面值   不是null true false 返回
	LEPT_PARSE_ROOT_NOT_SINGULAR,//若一个值之后，在空白之后还有其他字符
	LEPT_PARSE_NUMBER_TOO_BIG,//数字过大
	LEPT_PARSE_MISS_QUOTATION_MARK,//缺少引号
	LEPT_PARSE_INVALID_STRING_ESCAPE,//无效的字符串转移
	LEPT_PARSE_INVALID_STRING_CHAR,// 无效的字符串字符值
	LEPT_PARSE_INVALID_UNICODE_HEX,//如果 \u 后不是 4 位十六进位数字
	LEPT_PARSE_INVALID_UNICODE_SURROGATE,//如果只有高代理项而欠缺低代理项 或是 低代理项不在合法码点范围
	LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,//数组缺少逗号或中括号
	LEPT_PARSE_MISS_KEY,//缺少键
	LEPT_PARSE_MISS_COLON,//缺少冒号
	LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET//缺少逗号或花括号
};

//将类型变成null  可以避免重复释放
#define lept_init(v) do { (v)->type = LEPT_NULL; } while(0)

int lept_parse(lept_value* v, const char* json);
char* lept_stringify(const lept_value* v, size_t* length);

void lept_copy(lept_value* dst, const lept_value* src);
void lept_move(lept_value* dst, lept_value* src);
void lept_swap(lept_value* lhs, lept_value* rhs);

void lept_free(lept_value* v);

lept_type lept_get_type(const lept_value* v);
int lept_is_equal(const lept_value* lhs, const lept_value* rhs);

//写入null 调用lept_free将值类型变为null
#define lept_set_null(v) lept_free(v)

int lept_get_boolean(const lept_value* v);
void lept_set_boolean(lept_value* v, int b);

double lept_get_number(const lept_value* v);
void lept_set_number(lept_value* v, double n);

const char* lept_get_string(const lept_value* v);
size_t lept_get_string_length(const lept_value* v);
void lept_set_string(lept_value* v, const char* s, size_t len);

void lept_set_array(lept_value* v, size_t capacity);
size_t lept_get_array_size(const lept_value* v);
size_t lept_get_array_capacity(const lept_value* v);
void lept_reserve_array(lept_value* v, size_t capacity);
void lept_shrink_array(lept_value* v);
void lept_clear_array(lept_value* v);
lept_value* lept_get_array_element(lept_value* v, size_t index);
lept_value* lept_pushback_array_element(lept_value* v);
void lept_popback_array_element(lept_value* v);
lept_value* lept_insert_array_element(lept_value* v, size_t index);
void lept_erase_array_element(lept_value* v, size_t index, size_t count);

void lept_set_object(lept_value* v, size_t capacity);
size_t lept_get_object_size(const lept_value* v);
size_t lept_get_object_capacity(const lept_value* v);
void lept_reserve_object(lept_value* v, size_t capacity);
void lept_shrink_object(lept_value* v);
void lept_clear_object(lept_value* v);
const char* lept_get_object_key(const lept_value* v, size_t index);
size_t lept_get_object_key_length(const lept_value* v, size_t index);
lept_value* lept_get_object_value(lept_value* v, size_t index);
size_t lept_find_object_index(const lept_value* v, const char* key, size_t klen);
lept_value* lept_find_object_value(lept_value* v, const char* key, size_t klen);
lept_value* lept_set_object_value(lept_value* v, const char* key, size_t klen);
void lept_remove_object_value(lept_value* v, size_t index);

#endif /* LEPTJSON_H__ */
