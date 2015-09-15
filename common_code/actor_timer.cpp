#include "actor_timer.h"
#include "scattered.h"
#include "actor_framework.h"
#include <boost/asio/high_resolution_timer.hpp>

typedef boost::asio::basic_waitable_timer<boost::chrono::high_resolution_clock> timer_type;

ActorTimer_::ActorTimer_(const shared_strand& strand)
:_ios(strand->get_io_engine()), _looping(false), _weakStrand(strand), _timerCount(0),
_extMaxTick(-1), _extFinishTime(-1), _timer(_ios.getTimer()), _listAlloc(8192), _handlerTable(4096)
{
	_listPool = create_shared_pool<msg_list_shared_alloc<actor_handle, null_mutex>>(4096, [this](void* p)
	{
		new(p)msg_list<actor_handle, list_alloc>(_listAlloc);
	});
}

ActorTimer_::~ActorTimer_()
{
	assert(_handlerTable.empty());
	assert(!_strand);
	_ios.freeTimer(_timer);
	delete _listPool;
}

ActorTimer_::timer_handle ActorTimer_::timeout(unsigned long long us, const actor_handle& host)
{
	if (!_strand)
	{
		_strand = _weakStrand.lock();
	}
	assert(_strand->running_in_this_thread());
	assert(us < 0x80000000LL * 1000);
	unsigned long long et = (get_tick_us() + us) & -256;
	timer_handle timerHandle;
	if (et >= _extMaxTick)
	{
		_extMaxTick = et;
		timerHandle._tableNode = _handlerTable.insert(_handlerTable.end(), make_pair(et, handler_list()));
	}
	else
	{
		timerHandle._tableNode = _handlerTable.insert(make_pair(et, handler_list())).first;
	}
	handler_list& hl = timerHandle._tableNode->second;
	if (!hl)
	{
		hl = _listPool->pick();
		assert(hl->empty());
	}
	hl->push_front(host);
	timerHandle._handlerNode = hl->begin();
	timerHandle._handlerList = hl;
	
	if (!_looping)
	{//定时器已经退出循环，重新启动定时器
		_looping = true;
		assert(_handlerTable.size() == 1);
		_extFinishTime = et;
		timer_loop(us);
	}
	else if (et < _extFinishTime)
	{//定时期限前于当前定时器期限，取消后重新计时
		boost::system::error_code ec;
		((timer_type*)_timer)->cancel(ec);
		_timerCount++;
		_extFinishTime = et;
		timer_loop(us);
	}
	return timerHandle;
}

void ActorTimer_::cancel(timer_handle& th)
{
	auto* hl = th._handlerList.get();
	if (hl)
	{//删除当前定时器节点
		assert(_strand && _strand->running_in_this_thread());
		hl->erase(th._handlerNode);
		if (hl->empty())
		{
			if (_handlerTable.size() == 1)
			{
				_extMaxTick = -1;
				_handlerTable.erase(th._tableNode);
				//如果没有定时任务就退出定时循环
				boost::system::error_code ec;
				((timer_type*)_timer)->cancel(ec);
				_timerCount++;
				_looping = false;
			}
			else if (th._tableNode->first == _extMaxTick)
			{
				_handlerTable.erase(th._tableNode--);
				_extMaxTick = th._tableNode->first;
			}
			else
			{
				_handlerTable.erase(th._tableNode);
			}
		}
		th._handlerList.reset();
	}
}

void ActorTimer_::timer_loop(unsigned long long us)
{
	int tc = ++_timerCount;
	boost::system::error_code ec;
	((timer_type*)_timer)->expires_from_now(boost::chrono::microseconds(us), ec);
	((timer_type*)_timer)->async_wait(_strand->wrap_asio([this, tc](const boost::system::error_code&)
	{
		assert(_strand->running_in_this_thread());
		if (tc == _timerCount)
		{
			_extFinishTime = 0;
			unsigned long long nt = get_tick_us();
			while (!_handlerTable.empty())
			{
				auto iter = _handlerTable.begin();
				if (iter->first > nt + 500)
				{
					_extFinishTime = iter->first;
					timer_loop(iter->first - nt);
					return;
				}
				else
				{
					handler_list hl;
					iter->second.swap(hl);
					_handlerTable.erase(iter);
					for (auto it = hl->begin(); it != hl->end(); it++)
					{
						(*it)->timeout_handler();
					}
					hl->clear();
				}
			}
			_looping = false;
			_strand.reset();
		}
		else if (tc == _timerCount - 1)
		{
			_strand.reset();
		}
	}));
}