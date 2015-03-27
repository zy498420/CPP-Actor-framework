#ifndef __SCATTERED_H
#define __SCATTERED_H

/*!
@brief ���ø߾���ʱ��
*/
void enable_high_resolution();

/*!
@brief ��������Ȩ����Ϊ����ʵʱ��
*/
void enable_realtime_priority();

/*!
@brief ���ó������ȼ�
REALTIME_PRIORITY_CLASS
HIGH_PRIORITY_CLASS
ABOVE_NORMAL_PRIORITY_CLASS
NORMAL_PRIORITY_CLASS
BELOW_NORMAL_PRIORITY_CLASS
IDLE_PRIORITY_CLASS
*/
void set_priority(int p);

/*!
@brief ��ȡӲ��ʱ���
*/
long long get_tick_us();
long long get_tick_ms();
int get_tick_s();

/*!
@brief ���std::function
*/
template <typename F>
inline void clear_function(F& f)
{
	f = F();
}

#endif