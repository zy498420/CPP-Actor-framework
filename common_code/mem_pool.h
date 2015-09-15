#ifndef __MEM_POOL_H
#define __MEM_POOL_H

#include <boost/thread/mutex.hpp>
#include <boost/thread/lock_guard.hpp>

struct null_mutex
{
	void inline lock() const {};
	void inline unlock() const {};
};

struct mem_alloc_base 
{
	mem_alloc_base(){}
	virtual ~mem_alloc_base(){}
	virtual void* allocate() = 0;
	virtual void deallocate(void* p) = 0;
	virtual bool shared() const = 0;
	virtual size_t alloc_size() const = 0;

	bool full() const
	{
		return _blockNumber >= _poolMaxSize;
	}

	size_t _nodeCount;
	size_t _poolMaxSize;
	size_t _blockNumber;
private:
	mem_alloc_base(const mem_alloc_base&){}
	void operator=(const mem_alloc_base&){}
};

//////////////////////////////////////////////////////////////////////////

template <typename DATA, typename MUTEX = boost::mutex>
struct mem_alloc_mt: mem_alloc_base
{
	struct node_space;

	union BUFFER
	{
		unsigned char _space[sizeof(DATA)];
		node_space* _link;
	};

	struct node_space
	{
		void set_bf()
		{
#ifdef _DEBUG
			memset(get_ptr(), 0xBF, sizeof(DATA));
#endif
		}

		void set_af()
		{
#ifdef _DEBUG
			memset(get_ptr(), 0xAF, sizeof(DATA));
#endif
		}

		void* get_ptr()
		{
			return _buff._space;
		}

		void set_head()
		{
#ifdef _DEBUG
			_size = sizeof(DATA);
#endif
		}

		void check_head()
		{
			assert(sizeof(DATA) <= _size);
		}

		static node_space* get_node(void* p)
		{
#ifdef _DEBUG
			return (node_space*)((unsigned char*)p - sizeof(size_t));
#else
			return (node_space*)p;
#endif
		}

#ifdef _DEBUG
		size_t _size;
#endif
		BUFFER _buff;
	};

	mem_alloc_mt(size_t poolSize)
	{
		_nodeCount = 0;
		_poolMaxSize = poolSize;
		_pool = NULL;
		_blockNumber = 0;
	}

	virtual ~mem_alloc_mt()
	{
		boost::lock_guard<MUTEX> lg(_mutex);
		node_space* pIt = _pool;
		while (pIt)
		{
			assert(_nodeCount > 0);
			_nodeCount--;
			node_space* t = pIt;
			pIt = pIt->_buff._link;
			free(t);
		}
		assert(0 == _nodeCount);
	}

	void* allocate()
	{
		{
			boost::lock_guard<MUTEX> lg(_mutex);
			_blockNumber++;
			if (_pool)
			{
				_nodeCount--;
				node_space* fixedSpace = _pool;
				_pool = fixedSpace->_buff._link;
				fixedSpace->set_af();
				return fixedSpace->get_ptr();
			}
		}
		node_space* p = (node_space*)malloc(sizeof(node_space));
		p->set_head();
		return p->get_ptr();
	}

	void deallocate(void* p)
	{
		node_space* space = node_space::get_node(p);
		space->check_head();
		{
			boost::lock_guard<MUTEX> lg(_mutex);
			_blockNumber--;
			if (_nodeCount < _poolMaxSize)
			{
				_nodeCount++;
				space->set_bf();
				space->_buff._link = _pool;
				_pool = space;
				return;
			}
		}
		free(space);
	}

	size_t alloc_size() const
	{
		return sizeof(DATA);
	}

	bool shared() const
	{
		return true;
	}

	node_space* _pool;
	MUTEX _mutex;
};

//////////////////////////////////////////////////////////////////////////
template <typename DATA>
struct mem_alloc : public mem_alloc_mt<DATA, null_mutex>
{
	mem_alloc(size_t poolSize)
	:mem_alloc_mt(poolSize) {}
};

//////////////////////////////////////////////////////////////////////////

template <typename MUTEX = boost::mutex>
class reusable_mem_mt
{
	struct node 
	{
		unsigned _size;
		node* _next;
	};
public:
	reusable_mem_mt()
	{
		_top = NULL;
#ifdef _DEBUG
		_nodeCount = 0;
#endif
	}

	virtual ~reusable_mem_mt()
	{
		boost::lock_guard<MUTEX> lg(_mutex);
		while (_top)
		{
			assert(_nodeCount-- > 0);
			void* t = _top;
			_top = _top->_next;
			free(t);
		}
		assert(0 == _nodeCount);
	}

	void* allocate(size_t size)
	{
		void* freeMem = NULL;
		{
			boost::lock_guard<MUTEX> lg(_mutex);
			if (_top)
			{
				node* res = _top;
				_top = _top->_next;
				if (res->_size >= size)
				{
					return res;
				}
				assert(_nodeCount-- > 0);
				freeMem = res;
			}
#ifdef _DEBUG
			_nodeCount++;
#endif
		}
		if (freeMem)
		{
			free(freeMem);
		}
		return malloc(size < sizeof(node) ? sizeof(node) : size);
	}

	void deallocate(void* p, size_t size)
	{
		boost::lock_guard<MUTEX> lg(_mutex);
		node* dp = (node*)p;
		dp->_size = size < sizeof(node) ? sizeof(node) : (unsigned)size;
		dp->_next = _top;
		_top = dp;
	}
private:
	node* _top;
	MUTEX _mutex;
#ifdef _DEBUG
	size_t _nodeCount;
#endif
};
//////////////////////////////////////////////////////////////////////////

class reusable_mem : public reusable_mem_mt<null_mutex> {};

//////////////////////////////////////////////////////////////////////////
template<class _Ty, class _Mtx = boost::mutex>
class pool_alloc_mt
{
public:
	typedef _Ty node_type;
	typedef typename mem_alloc<_Ty> mem_alloc_type;
	typedef typename mem_alloc_mt<_Ty, _Mtx> mem_alloc_mt_type;
	typedef typename std::allocator<_Ty>::pointer pointer;
	typedef typename std::allocator<_Ty>::difference_type difference_type;
	typedef typename std::allocator<_Ty>::reference reference;
	typedef typename std::allocator<_Ty>::const_pointer const_pointer;
	typedef typename std::allocator<_Ty>::const_reference const_reference;
	typedef typename std::allocator<_Ty>::size_type size_type;
	typedef typename std::allocator<_Ty>::value_type value_type;

	template<class _Other>
	struct rebind
	{
		typedef pool_alloc_mt<_Other, typename _Mtx> other;
	};

	pool_alloc_mt(size_t poolSize, bool shared = true)
	{
		if (shared)
		{
			_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_mt_type(poolSize));
		} 
		else
		{
			_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_type(poolSize));
		}
	}

	~pool_alloc_mt()
	{
	}

	pool_alloc_mt(const pool_alloc_mt& s)
	{
		if (s.is_shared())
		{
			_memAlloc = s._memAlloc;
		} 
		else
		{
			_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_type(s._memAlloc->_poolMaxSize));
		}
	}

	template<class _Other>
	pool_alloc_mt(const pool_alloc_mt<_Other, _Mtx>& s)
	{
		if (s.is_shared())
		{
// 			if (sizeof(_Ty) > s._memAlloc->alloc_size())
// 			{
// 				_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_mt_type(s._memAlloc->_poolMaxSize));
// 			} 
// 			else
			{
				assert(sizeof(_Ty) <= s._memAlloc->alloc_size());
				_memAlloc = s._memAlloc;
			}
		}
		else
		{
			_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_type(s._memAlloc->_poolMaxSize));
		}
	}

	pool_alloc_mt& operator=(const pool_alloc_mt& s)
	{
		assert(false);
		return *this;
	}

	template<class _Other>
	pool_alloc_mt& operator=(const pool_alloc_mt<_Other, _Mtx>& s)
	{
		assert(false);
		return *this;
	}

	bool operator==(const pool_alloc_mt& s)
	{
		return is_shared() == s.is_shared();
	}

	void deallocate(pointer _Ptr, size_type)
	{
		_memAlloc->deallocate(_Ptr);
	}

	pointer allocate(size_type _Count)
	{
		assert(1 == _Count);
		return (pointer)_memAlloc->allocate();
	}

	void construct(_Ty *_Ptr, const _Ty& _Val)
	{
		new ((void *)_Ptr) _Ty(_Val);
	}

	void construct(_Ty *_Ptr, _Ty&& _Val)
	{
		new ((void *)_Ptr) _Ty(std::move(_Val));
	}

	template<class _Uty>
	void destroy(_Uty *_Ptr)
	{
		_Ptr->~_Uty();
	}

	size_t max_size() const
	{
		return ((size_t)(-1) / sizeof (_Ty));
	}

	bool is_shared() const
	{
		return _memAlloc->shared();
	}

	void enable_shared(size_t poolSize = -1)
	{
		if (!_memAlloc->shared())
		{
			_memAlloc = std::shared_ptr<mem_alloc_base>(new mem_alloc_mt_type(-1 == poolSize ? _memAlloc->_poolMaxSize : poolSize));
		}
	}

	std::shared_ptr<mem_alloc_base> _memAlloc;
};
//////////////////////////////////////////////////////////////////////////
template<class _Ty>
class pool_alloc
{
public:
	typedef _Ty node_type;
	typedef typename mem_alloc<_Ty> mem_alloc_type;
	typedef typename std::allocator<_Ty>::pointer pointer;
	typedef typename std::allocator<_Ty>::difference_type difference_type;
	typedef typename std::allocator<_Ty>::reference reference;
	typedef typename std::allocator<_Ty>::const_pointer const_pointer;
	typedef typename std::allocator<_Ty>::const_reference const_reference;
	typedef typename std::allocator<_Ty>::size_type size_type;
	typedef typename std::allocator<_Ty>::value_type value_type;

	template<class _Other>
	struct rebind
	{
		typedef pool_alloc<_Other> other;
	};

	pool_alloc(size_t poolSize)
		:_memAlloc(poolSize)
	{
	}

	~pool_alloc()
	{
	}

	pool_alloc(const pool_alloc& s)
		:_memAlloc(s._memAlloc._poolMaxSize)
	{
	}

	template<class _Other>
	pool_alloc(const pool_alloc<_Other>& s)
		:_memAlloc(s._memAlloc._poolMaxSize)
	{
	}

	pool_alloc& operator=(const pool_alloc& s)
	{
		assert(false);
		return *this;
	}

	template<class _Other>
	pool_alloc& operator=(const pool_alloc<_Other>& s)
	{
		assert(false);
		return *this;
	}

	bool operator==(const pool_alloc& s)
	{
		return true;
	}

	void deallocate(pointer _Ptr, size_type)
	{
		_memAlloc.deallocate(_Ptr);
	}

	pointer allocate(size_type _Count)
	{
		assert(1 == _Count);
		return (pointer)_memAlloc.allocate();
	}

	void construct(_Ty *_Ptr, const _Ty& _Val)
	{
		new ((void *)_Ptr) _Ty(_Val);
	}

	void construct(_Ty *_Ptr, _Ty&& _Val)
	{
		new ((void *)_Ptr) _Ty(std::move(_Val));
	}

	template<class _Uty>
	void destroy(_Uty *_Ptr)
	{
		_Ptr->~_Uty();
	}

	size_t max_size() const
	{
		return ((size_t)(-1) / sizeof (_Ty));
	}

	bool is_shared() const
	{
		return _memAlloc.shared();
	}

	mem_alloc_type _memAlloc;
};
//////////////////////////////////////////////////////////////////////////

template <typename T, typename CREATER, typename DESTORY, typename MUTEX = boost::mutex>
class SharedObjPool_;

template <typename T, typename CREATER, typename DESTORY, typename MUTEX = boost::mutex>
class ObjPool_;

template <typename T>
class obj_pool
{
public:
	virtual ~obj_pool(){};
	virtual	T* pick() = 0;
	virtual void recycle(void* p) = 0;
};

template <typename T, typename CREATER, typename DESTORY>
static obj_pool<T>* create_pool(size_t poolSize, const CREATER& creater, const DESTORY& destory)
{
	return new ObjPool_<T, CREATER, DESTORY>(poolSize, creater, destory);
}

template <typename T, typename CREATER>
static obj_pool<T>* create_pool(size_t poolSize, const CREATER& creater)
{
	return create_pool<T>(poolSize, creater, [](T* p)
	{
		typedef T type;
		p->~type();
	});
}

template <typename T, typename MUTEX, typename CREATER, typename DESTORY>
static obj_pool<T>* create_pool_mt(size_t poolSize, const CREATER& creater, const DESTORY& destory)
{
	return new ObjPool_<T, CREATER, DESTORY, MUTEX>(poolSize, creater, destory);
}

template <typename T, typename MUTEX, typename CREATER>
static obj_pool<T>* create_pool_mt(size_t poolSize, const CREATER& creater)
{
	return create_pool_mt<T, MUTEX>(poolSize, creater, [](T* p)
	{
		typedef T type;
		p->~type();
	});
}

template <typename T, typename CREATER, typename DESTORY, typename MUTEX>
class ObjPool_: public obj_pool<T>
{
	struct node 
	{
		unsigned char _data[sizeof(T)];
		node* _link;
	};

	friend static obj_pool<T>* create_pool<T, CREATER, DESTORY>(size_t, const CREATER&, const DESTORY&);
	friend static obj_pool<T>* create_pool_mt<T, MUTEX, CREATER, DESTORY>(size_t, const CREATER&, const DESTORY&);
	friend SharedObjPool_<T, CREATER, DESTORY, MUTEX>;
private:
	ObjPool_(size_t poolSize, const CREATER& creater, const DESTORY& destory)
		:_creater(creater), _destory(destory), _poolMaxSize(poolSize), _nodeCount(0), _link(NULL)
	{
#ifdef _DEBUG
		_blockNumber = 0;
#endif
	}
public:
	~ObjPool_()
	{
		boost::lock_guard<MUTEX> lg(_mutex);
		assert(0 == _blockNumber);
		node* it = _link;
		while (it)
		{
			assert(_nodeCount > 0);
			_nodeCount--;
			node* t = it;
			it = it->_link;
			_destory((T*)t->_data);
			free(t);
		}
		assert(0 == _nodeCount);
	}
public:
	T* pick()
	{
		{
			boost::lock_guard<MUTEX> lg(_mutex);
#ifdef _DEBUG
			_blockNumber++;
#endif
			if (_link)
			{
				_nodeCount--;
				node* r = _link;
				_link = _link->_link;
				return (T*)r->_data;
			}
		}
		node* newNode = (node*)malloc(sizeof(node));
		assert((void*)newNode == (void*)newNode->_data);
		_creater(newNode);
		return (T*)newNode->_data;
	}

	void recycle(void* p)
	{
		{
			boost::lock_guard<MUTEX> lg(_mutex);
#ifdef _DEBUG
			_blockNumber--;
#endif
			if (_nodeCount < _poolMaxSize)
			{
				_nodeCount++;
				((node*)p)->_link = _link;
				_link = (node*)p;
				return;
			}
		}
		_destory((T*)((node*)p)->_data);
		free(p);
	}
private:
	CREATER _creater;
	DESTORY _destory;
	MUTEX _mutex;
	node* _link;
	size_t _poolMaxSize;
	size_t _nodeCount;
#ifdef _DEBUG
	size_t _blockNumber;
#endif
};
//////////////////////////////////////////////////////////////////////////

template <typename T>
class shared_obj_pool
{
public:
	virtual ~shared_obj_pool(){};
	virtual	std::shared_ptr<T> pick() = 0;
};

template <typename T, typename CREATER, typename DESTORY>
static shared_obj_pool<T>* create_shared_pool(size_t poolSize, const CREATER& creater, const DESTORY& destory)
{
	return new SharedObjPool_<T, CREATER, DESTORY>(poolSize, creater, destory);
}

template <typename T, typename CREATER>
static shared_obj_pool<T>* create_shared_pool(size_t poolSize, const CREATER& creater)
{
	return create_shared_pool<T>(poolSize, creater, [](T* p)
	{
		typedef T type;
		p->~type();
	});
}

template <typename T, typename MUTEX, typename CREATER, typename DESTORY>
static shared_obj_pool<T>* create_shared_pool_mt(size_t poolSize, const CREATER& creater, const DESTORY& destory)
{
	return new SharedObjPool_<T, CREATER, DESTORY, MUTEX>(poolSize, creater, destory);
}

template <typename T, typename MUTEX, typename CREATER>
static shared_obj_pool<T>* create_shared_pool_mt(size_t poolSize, const CREATER& creater)
{
	return create_shared_pool_mt<T, MUTEX>(poolSize, creater, [](T* p)
	{
		typedef T type;
		p->~type();
	});
}

template <typename T, typename CREATER, typename DESTORY, typename MUTEX>
class SharedObjPool_: public shared_obj_pool<T>
{
	template <typename RC>
	struct create_alloc 
	{
		template<class Other>
		struct rebind
		{
			typedef create_alloc<Other> other;
		};

		create_alloc(void*& refCountAlloc, size_t poolSize)
			:_refCountAlloc(refCountAlloc), _nodeCount(poolSize) {}

		create_alloc(const create_alloc<RC>& s)
			:_refCountAlloc(s._refCountAlloc), _nodeCount(s._nodeCount) {}

		template<class Other>
		create_alloc(const create_alloc<Other>& s)
			: _refCountAlloc(s._refCountAlloc), _nodeCount(s._nodeCount) {}

		RC* allocate(size_t count)
		{
			assert(1 == count);
			_refCountAlloc = new mem_alloc_mt<RC, MUTEX>(_nodeCount);
			return (RC*)malloc(sizeof(RC));
		}

		void deallocate(RC* ptr, size_t count)
		{
			assert(1 == count);
			free(ptr);
			delete (mem_alloc_mt<RC, MUTEX>*)_refCountAlloc;
			_refCountAlloc = NULL;
		}

		void destroy(RC* ptr)
		{
			ptr->~RC();
		}

		void*& _refCountAlloc;
		size_t _nodeCount;
	};

	template <typename RC>
	struct ref_count_alloc 
	{
		template<class Other>
		struct rebind
		{
			typedef ref_count_alloc<Other> other;
		};

		ref_count_alloc(void* refCountAlloc)
			:_refCountAlloc(refCountAlloc) {}

		ref_count_alloc(const ref_count_alloc<RC>& s)
			:_refCountAlloc(s._refCountAlloc) {}

		template<class Other>
		ref_count_alloc(const ref_count_alloc<Other>& s)
			: _refCountAlloc(s._refCountAlloc) {}

		RC* allocate(size_t count)
		{
			assert(1 == count);
			return (RC*)((mem_alloc_mt<RC, MUTEX>*)_refCountAlloc)->allocate();
		}

		void deallocate(RC* ptr, size_t count)
		{
			assert(1 == count);
			((mem_alloc_mt<RC, MUTEX>*)_refCountAlloc)->deallocate(ptr);
		}

		void destroy(RC* ptr)
		{
			ptr->~RC();
		}

		void* _refCountAlloc;
	};

	friend static shared_obj_pool<T>* create_shared_pool<T, CREATER, DESTORY>(size_t, const CREATER&, const DESTORY&);
	friend static shared_obj_pool<T>* create_shared_pool_mt<T, MUTEX, CREATER, DESTORY>(size_t, const CREATER&, const DESTORY&);
private:
	SharedObjPool_(size_t poolSize, const CREATER& creater, const DESTORY& destory)
		:_dataAlloc(poolSize, creater, destory)
	{
		_lockAlloc = std::shared_ptr<T>(NULL, [](T*){}, create_alloc<void>(_refCountAlloc, poolSize));
	}
public:
	~SharedObjPool_()
	{
		_lockAlloc.reset();
		assert(!_refCountAlloc);
	}
public:
	std::shared_ptr<T> pick()
	{
		return std::shared_ptr<T>(_dataAlloc.pick(), [this](T* p){_dataAlloc.recycle(p); }, ref_count_alloc<void>(_refCountAlloc));
	}
private:
	ObjPool_<T, CREATER, DESTORY, MUTEX> _dataAlloc;
	std::shared_ptr<T> _lockAlloc;
	void* _refCountAlloc;
};

#endif