#ifndef __ACTOR_TIMER_H
#define __ACTOR_TIMER_H

#include <algorithm>
#include "shared_strand.h"
#include "msg_queue.h"
#include "stack_object.h"

class boost_strand;
class qt_strand;
class my_actor;

/*!
@brief Actor 内部使用的定时器
*/
class ActorTimer_
#ifdef DISABLE_BOOST_TIMER
	: public TimerBoostCompletedEventFace_
#endif
{
	typedef std::shared_ptr<my_actor> actor_handle;
	typedef msg_multimap<unsigned long long, actor_handle> handler_queue;

	friend boost_strand;
	friend qt_strand;
	friend my_actor;

	class timer_handle 
	{
		friend ActorTimer_;
	public:
		void reset()
		{
			_null = true;
		}
	private:
		bool _null = true;
		handler_queue::iterator _queueNode;
	};
private:
	ActorTimer_(const shared_strand& strand);
	~ActorTimer_();
private:
	/*!
	@brief 开始计时
	@param us 微秒
	@param host 准备计时的Actor
	@return 计时句柄，用于cancel
	*/
	timer_handle timeout(unsigned long long us, actor_handle&& host);

	/*!
	@brief 取消计时
	*/
	void cancel(timer_handle& th);

	/*!
	@brief timer循环
	*/
	void timer_loop(unsigned long long us);

	/*!
	@brief timer事件
	*/
	void event_handler(int tc);
#ifdef DISABLE_BOOST_TIMER
	void post_event(int tc);
#endif
private:
	void* _timer;
	std::weak_ptr<boost_strand>& _weakStrand;
	shared_strand _lockStrand;
	handler_queue _handlerQueue;
	unsigned long long _extMaxTick;
	unsigned long long _extFinishTime;
#ifdef DISABLE_BOOST_TIMER
	stack_obj<boost::asio::io_service::work, false> _lockIos;
#endif
	int _timerCount;
	bool _looping;
};

#endif