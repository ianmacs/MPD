/* The following conditions apply to the code in this file, SoundSender.hh
   All Code in this file written by IanMacs in 2015

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>

*/

#ifndef SOUNDSENDER_HH
#define SOUNDSENDER_HH

#include <stdint.h> // for int64_t in Clock declaration
#include <time.h>   // for Clock implementation
#include <algorithm>// for std::min in Pacer implementation

namespace SoundSender {

  // Clock declaration
  class Clock {
  public:
    typedef int64_t nsec_t;
    nsec_t get_nsec() const;
  };

  // Pacer declaration
  class Pacer {
  public:
    enum State {READY, WAITING, OVERDUE, ABORTED};

    State get_state() const {
      if (last_trigger_time == 0)
        return READY;
      Clock::nsec_t now = clock.get_nsec();
      if ((now - last_trigger_time) < period)
        return WAITING;
      if ((now - last_trigger_time) > (period + duetimeslot))
        return ABORTED;
      return OVERDUE;
    }
    Clock::nsec_t get_sleeptime() const {
      switch (get_state()) {
      case READY:
      case OVERDUE:
      case ABORTED:
        return 0;
      default:
        return last_trigger_time + period - clock.get_nsec();
      }
    }
    Clock::nsec_t get_duetimeslot() const {
      return duetimeslot;
    }
    Clock::nsec_t get_period() const {
      return period;
    }
    void trigger() {
      switch (get_state()) {
      case WAITING:
      case OVERDUE:
        last_trigger_time += period;
        break;
      default:
        last_trigger_time = clock.get_nsec();
      }
    }
    Pacer(Clock clock, 
          Clock::nsec_t ns = Clock::nsec_t(256) * 1000000000 / 48000,
          Clock::nsec_t dt = 0);
  private:
    Clock clock;
    Clock::nsec_t period;
    Clock::nsec_t duetimeslot;
    Clock::nsec_t last_trigger_time;
  };

  Pacer::State operator++(Pacer::State & s);

  // Clock implementation
  Clock::nsec_t Clock::get_nsec() const {
    nsec_t nsec = 0;
    struct timespec ts = {0,0};
    clock_gettime(CLOCK_REALTIME, &ts);
    nsec = ts.tv_sec;
    nsec *= 1000000000;
    nsec += ts.tv_nsec;
    return nsec;
  }

  // Pacer implementation
  Pacer::Pacer(Clock clock_param, Clock::nsec_t ns, Clock::nsec_t dt) {
    this->clock = clock_param;
    this->last_trigger_time = 0;
    if (dt == 0)
      dt = std::min<Clock::nsec_t>(1000000, ns/2);
    duetimeslot = dt;
    period = ns;
  }

  Pacer::State operator++(Pacer::State & s) {
    return s = static_cast<Pacer::State>(s+1);
  }
}

#endif
