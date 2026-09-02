#pragma once
#include <cstddef>
#include <deque>
#include <utility>
namespace opennow {
template<class T> class BoundedQueue {
 public: explicit BoundedQueue(std::size_t capacity=1):cap_(capacity?capacity:1),drops_(0){}
 bool push(T v){bool dropped=false;while(q_.size()>=cap_){q_.pop_front();++drops_;dropped=true;}q_.push_back(v);return dropped;}
 bool pop(T& out){if(q_.empty())return false;out=q_.front();q_.pop_front();return true;}
 void clear(){q_.clear();} std::size_t size()const{return q_.size();} std::size_t drops()const{return drops_;}
 private:std::size_t cap_,drops_;std::deque<T> q_;
};}
