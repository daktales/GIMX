/*
 Copyright (c) 2011 Mathieu Laurendeau <mat.lau@laposte.net>
 License: GPLv3
 */

#include "event_catcher.h"
#include <unistd.h>
#include <sstream>
#include <ginput.h>
#include <gpoll.h>
#include <gtimer.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#ifdef WIN32
#define REGISTER_FUNCTION gpoll_register_handle
#define REMOVE_FUNCTION gpoll_remove_handle
#else
#define REGISTER_FUNCTION gpoll_register_fd
#define REMOVE_FUNCTION gpoll_remove_fd
#endif

#define PERIOD 10000//microseconds

event_catcher* event_catcher::_singleton = NULL;

event_catcher::event_catcher() : done(0), stopTimer(-1), wevents(false)
{
    //ctor
}

event_catcher::~event_catcher()
{
    //dtor
}

int process_event(GE_Event* event);

int event_catcher::init()
{
    unsigned char src = GE_MKB_SOURCE_PHYSICAL;
    
    if(wevents)
    {
      src = GE_MKB_SOURCE_WINDOW_SYSTEM;
    }
    
    GPOLL_INTERFACE poll_interace = {
            .fp_register = REGISTER_FUNCTION,
            .fp_remove = REMOVE_FUNCTION,
    };
    if(ginput_init(&poll_interace, src, process_event) < 0)
    {
      return -1;
    }

    return 0;
}

bool event_catcher::check_device(string device_type, string device_name, string device_id)
{
    int i = 0;
    int nb = 0;
    stringstream ss;
    int did;
    const char * name;

    ss << device_id;
    ss >> did;

    if(device_type == "keyboard")
    {
      while((name = ginput_keyboard_name(i)))
      {
        if(name == device_name)
        {
          if(nb == did)
          {
            return true;
          }
          else
          {
            nb++;
          }
        }
        i++;
      }
    }
    else if(device_type == "mouse")
    {
      while((name = ginput_mouse_name(i)))
      {
        if(name == device_name)
        {
          if(nb == did)
          {
            return true;
          }
          else
          {
            nb++;
          }
        }
        i++;
      }
    }
    else if(device_type == "joystick")
    {
      while((name = ginput_joystick_name(i)))
      {
        if(name == device_name)
        {
          if(nb == did)
          {
            return true;
          }
          else
          {
            nb++;
          }
        }
        i++;
      }
    }
    return false;
}

void event_catcher::clean()
{
    if (stopTimer >= 0)
    {
        gtimer_close(stopTimer);
        stopTimer = -1;
    }
    ginput_quit();
}

typedef struct
{
  unsigned char which;
  unsigned char axis;
  short value;
} s_joystick_axis_first;

/*
 * This function records the first value for each axis of each joystick.
 * If a first value was already recorded, it returns the absolute difference
 * between the actual value and the first value.
 * If the first value is recorded, it returns 0.
 */
static int process_joystick_axis(s_joystick_axis_first* axis_first, unsigned int* axis_first_nb, GE_Event* event)
{
  int diff = 0;
  s_joystick_axis_first* last;
  for(last=axis_first; last<axis_first+*axis_first_nb; ++last)
  {
    if(event->jaxis.which == last->which
        && event->jaxis.axis == last->axis)
    {
      diff = abs(event->jaxis.value - last->value);
      break;
    }
  }
  if(last == axis_first+*axis_first_nb)
  {
    if(*axis_first_nb < EVENT_BUFFER_SIZE)
    {
      axis_first[*axis_first_nb].which = event->jaxis.which;
      axis_first[*axis_first_nb].axis = event->jaxis.axis;
      axis_first[*axis_first_nb].value = event->jaxis.value;
      ++(*axis_first_nb);
    }
  }
  return diff;
}

static unsigned int axis_first_nb;
static s_joystick_axis_first axis_first[EVENT_BUFFER_SIZE];

int process_event(GE_Event* event)
{
  event_catcher* evcatch = event_catcher::getInstance();

  if(evcatch->GetDone())
  {
    return 0;
  }

  switch (event->type)
  {
  case GE_KEYDOWN:
      if(evcatch->GetDeviceType() != "" && evcatch->GetDeviceType() != "keyboard")
      {
          break;
      }
      if(evcatch->GetEventType() == "button")
      {
          stringstream ss1;
          ss1 << ginput_keyboard_virtual_id(event->key.which);
          Device dev("keyboard", ss1.str(), ginput_keyboard_name(event->key.which));
          Event ev("button", ginput_key_name(event->key.keysym));
          evcatch->AddEvent(dev, ev);
          evcatch->StartTimer();
      }
      break;
  case GE_MOUSEBUTTONDOWN:
      if(evcatch->GetDeviceType() != "" && evcatch->GetDeviceType() != "mouse")
      {
          break;
      }
      if(evcatch->GetEventType() == "button")
      {
          stringstream ss1;
          ss1 << ginput_mouse_virtual_id(event->button.which);
          Device dev("mouse", ss1.str(), ginput_mouse_name(event->button.which));
          Event ev("button", ginput_mouse_button_name(event->button.button));
          evcatch->AddEvent(dev, ev);
          evcatch->StartTimer();
      }
      break;
  case GE_JOYBUTTONDOWN:
      if(evcatch->GetDeviceType() != "" && evcatch->GetDeviceType() != "joystick")
      {
          break;
      }
      if(evcatch->GetEventType() == "button")
      {
          stringstream ss1;
          ss1 << ginput_joystick_virtual_id(event->jbutton.which);
          Device dev("joystick", ss1.str(), ginput_joystick_name(event->jbutton.which));
          int ib = event->jbutton.button;
          stringstream ss2;
          ss2 << ib;
          Event ev("button", ss2.str());
          evcatch->AddEvent(dev, ev);
          evcatch->StartTimer();
      }
      break;
  case GE_MOUSEMOTION:
      if(evcatch->GetDeviceType() != "" && evcatch->GetDeviceType() != "mouse")
      {
          break;
      }
      if(evcatch->GetEventType() == "axis")
      {
          if(abs(event->motion.xrel) > 5 || abs(event->motion.yrel) > 5)
          {
              stringstream ss1;
              ss1 << ginput_mouse_virtual_id(event->motion.which);
              Device dev("mouse", ss1.str(), ginput_mouse_name(event->motion.which));
              const char * axis;
              if(abs(event->motion.xrel) > abs(event->motion.yrel))
              {
                  axis = "x";
              }
              else
              {
                  axis = "y";
              }
              Event ev("axis", axis);
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      else if(evcatch->GetEventType() == "axis up")
      {
          if(event->motion.xrel > 5 || event->motion.yrel > 5)
          {
              stringstream ss1;
              ss1 << ginput_mouse_virtual_id(event->motion.which);
              Device dev("mouse", ss1.str(), ginput_mouse_name(event->motion.which));
              const char * axis;
              if(abs(event->motion.xrel) > abs(event->motion.yrel))
              {
                  axis = "x";
              }
              else
              {
                  axis = "y";
              }
              Event ev("axis", axis);
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      else if(evcatch->GetEventType() == "axis down")
      {
          if(event->motion.xrel < -5 || event->motion.yrel < -5)
          {
              stringstream ss1;
              ss1 << ginput_mouse_virtual_id(event->motion.which);
              Device dev("mouse", ss1.str(), ginput_mouse_name(event->motion.which));
              const char * axis;
              if(abs(event->motion.xrel) > abs(event->motion.yrel))
              {
                  axis = "x";
              }
              else
              {
                  axis = "y";
              }
              Event ev("axis", axis);
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      break;
  case GE_JOYAXISMOTION:
      if(evcatch->GetDeviceType() != "" && evcatch->GetDeviceType() != "joystick")
      {
          break;
      }
      if(evcatch->GetEventType() == "axis")
      {
          if(process_joystick_axis(axis_first, &axis_first_nb, event) > 16383)
          {
              stringstream ss1;
              ss1 << ginput_joystick_virtual_id(event->jaxis.which);
              Device dev("joystick", ss1.str(), ginput_joystick_name(event->jaxis.which));
              int ib = event->jaxis.axis;
              stringstream ss2;
              ss2 << ib;
              Event ev("axis", ss2.str());
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      else if(evcatch->GetEventType() == "axis up")
      {
          if(event->jaxis.value > 16383)
          {
              stringstream ss1;
              ss1 << ginput_joystick_virtual_id(event->jaxis.which);
              Device dev("joystick", ss1.str(), ginput_joystick_name(event->jaxis.which));
              int ib = event->jaxis.axis;
              stringstream ss2;
              ss2 << ib;
              Event ev("axis", ss2.str());
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      else if(evcatch->GetEventType() == "axis down")
      {
          if(event->jaxis.value < -16383)
          {
              stringstream ss1;
              ss1 << ginput_joystick_virtual_id(event->jaxis.which);
              Device dev("joystick", ss1.str(), ginput_joystick_name(event->jaxis.which));
              int ib = event->jaxis.axis;
              stringstream ss2;
              ss2 << ib;
              Event ev("axis", ss2.str());
              evcatch->AddEvent(dev, ev);
              evcatch->StartTimer();
          }
      }
      break;
  }

  return 0;
}

static int timer_read(int user __attribute__((unused)))
{
  return 1;
}

static int timer_close(int timer __attribute__((unused)))
{
  event_catcher::getInstance()->SetDone();
  return 1;
}

void event_catcher::StartTimer()
{
    if (stopTimer < 0)
    {
        GTIMER_CALLBACKS callbacks = {
                .fp_read = timer_close,
                .fp_close = timer_close,
                .fp_register = REGISTER_FUNCTION,
                .fp_remove = REMOVE_FUNCTION,
        };
        stopTimer = gtimer_start(0, 150000, &callbacks);
        if (stopTimer < 0)
        {
          done = 1;
        }
    }
}

void event_catcher::run(string device_type, string event_type)
{
    axis_first_nb = 0;
    memset(axis_first, 0x00, sizeof(axis_first));

    m_DeviceType = device_type;
    m_EventType = event_type;

    vector<pair<Device, Event> >().swap(m_Events);

    init();

    ginput_grab();

    done = 0;

    GTIMER_CALLBACKS callbacks = {
            .fp_read = timer_read,
            .fp_close = timer_close,
            .fp_register = REGISTER_FUNCTION,
            .fp_remove = REMOVE_FUNCTION,
    };
    int timer = gtimer_start(0, PERIOD, &callbacks);
    if (timer < 0)
    {
      done = 1;
    }
	
    while (!done)
    {
        gpoll();

        ginput_periodic_task();
    }

    if (timer >= 0)
    {
      gtimer_close(timer);
    }

    clean();
}

void event_catcher::AddEvent(Device device, Event event)
{
    vector<pair<Device, Event> >::iterator it = find(m_Events.begin(), m_Events.end(), make_pair(device, event));
    if (it == m_Events.end())
    {
        m_Events.push_back(make_pair(device, event));
    }
}
