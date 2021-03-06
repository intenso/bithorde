/*
    Copyright 2013 <copyright holder> <email>

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/


#include "timer.h"

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

TimerService::TimerService(boost::asio::io_service& ioSvc)
	: _timer(ioSvc)
{
}

void TimerService::arm(boost::posix_time::ptime deadline, Timer* t)
{
	_timers.insert(std::pair<boost::posix_time::ptime,Timer*>(deadline, t));
	enable();
}

void TimerService::clear(const Timer* t)
{
	auto iter = _timers.begin();
	while (iter != _timers.end()) {
		auto current = iter++;
		if (current->second == t)
			_timers.erase(current);
	}
}

void TimerService::enable()
{
	if (_timers.size() > 0) {
		const auto& target_time = _timers.begin()->first;
		if (target_time != _timer.expires_at()) {
			_timer.expires_at(target_time);
			_timer.async_wait(boost::bind(&TimerService::invoke, shared_from_this(), boost::asio::placeholders::error()));
		}
	}
}

void TimerService::invoke(boost::system::error_code ec)
{
	if (ec) return;
	auto now(boost::posix_time::microsec_clock::universal_time());
	auto iter = _timers.begin();
	while (iter != _timers.end() && iter->first <= now) {
		iter->second->invoke(iter->first, now);
		_timers.erase(iter);
		iter = _timers.begin();
	}
	enable();
}

Timer::Timer(TimerService& ts, const Timer::Target& target)
	: _ts(ts), _target(target)
{
}

Timer::~Timer()
{
	clear();
}

void Timer::arm(boost::posix_time::ptime deadline)
{
	_ts.arm(deadline, this);
}

void Timer::arm(boost::posix_time::time_duration in)
{
	_ts.arm(boost::posix_time::microsec_clock::universal_time() + in, this);
}

void Timer::clear() {
	_ts.clear(this);
}

void Timer::invoke(const boost::posix_time::ptime& scheduled_at, const boost::posix_time::ptime& now)
{
	_target(now);
}

PeriodicTimer::PeriodicTimer(TimerService& ts, const Timer::Target& target, boost::posix_time::time_duration interval)
	: Timer(ts, target), _interval(interval)
{
	arm(_interval);
}

void PeriodicTimer::rearm(boost::posix_time::time_duration interval) {
	clear();
	_interval = interval;
	arm(_interval);
}

void PeriodicTimer::invoke(const boost::posix_time::ptime& scheduled_at, const boost::posix_time::ptime& now)
{
	arm(scheduled_at+_interval);
	Timer::invoke(scheduled_at, now);
}



