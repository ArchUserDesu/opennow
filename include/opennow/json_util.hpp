#pragma once
#include <jansson.h>
#include <stdexcept>
#include <string>
namespace opennow {
class JsonPtr {
public:
    JsonPtr():p_(NULL){} explicit JsonPtr(json_t* p):p_(p){} JsonPtr(json_t* p,void(*)(json_t*)):p_(p){}
    ~JsonPtr(){if(p_)json_decref(p_);} json_t* get()const{return p_;}
    void reset(json_t* p=NULL){if(p_)json_decref(p_);p_=p;}
public:
    JsonPtr(const JsonPtr& other):p_(other.p_){other.p_=NULL;}
    JsonPtr& operator=(const JsonPtr& other){if(this!=&other){reset();p_=other.p_;other.p_=NULL;}return *this;}
private: mutable json_t* p_;
};
inline JsonPtr parse_json(const std::string&s){json_error_t e;json_t*j=json_loadb(s.c_str(),s.size(),0,&e);if(!j)throw std::runtime_error(std::string("JSON: ")+e.text);return JsonPtr(j);}
inline std::string js(json_t*o,const char*k){json_t*v=o?json_object_get(o,k):NULL;const char*p=json_is_string(v)?json_string_value(v):NULL;return p?p:"";}
inline int ji(json_t*o,const char*k,int d=0){json_t*v=o?json_object_get(o,k):NULL;return json_is_integer(v)?(int)json_integer_value(v):d;}
inline bool jb(json_t*o,const char*k,bool d=false){json_t*v=o?json_object_get(o,k):NULL;return json_is_boolean(v)?json_is_true(v)!=0:d;}
inline std::string dump_json(json_t*j){char*p=json_dumps(j,JSON_COMPACT);std::string s=p?p:"";if(p)free(p);return s;}
}
