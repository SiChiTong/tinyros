#ifndef _TINYROS_SERVICE_CLIENT_H_
#define _TINYROS_SERVICE_CLIENT_H_
#include <stdint.h>
#include <rtthread.h>
#include "tiny_ros/tinyros_msgs/TopicInfo.h"
#include "tiny_ros/ros/publisher.h"
#include "tiny_ros/ros/subscriber.h"

namespace tinyros
{

template<typename MReq , typename MRes>
class ServiceClient : public Subscriber_
{
public:
  ServiceClient(tinyros::string topic_name) :
    pub(topic_name, &req, tinyros::tinyros_msgs::TopicInfo::ID_SERVICE_CLIENT + tinyros::tinyros_msgs::TopicInfo::ID_PUBLISHER)
{
    this->negotiated_ = false;
    this->srv_flag_ = true;
    this->topic_ = topic_name;
    this->call_resp = NULL;
    this->call_req = NULL;
    rt_mutex_init(&mutex_, "sc", RT_IPC_FLAG_FIFO);
    rt_mutex_init(&g_mutex_, "gsc", RT_IPC_FLAG_FIFO);
    rt_sem_init(&sem_, "sem", 0, RT_IPC_FLAG_FIFO);
}
  virtual bool call(MReq & request, MRes & response, int duration = 3)
  {
    bool ret = false;
    rt_mutex_take(&g_mutex_, RT_WAITING_FOREVER);
    rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    do {
      if (!pub.nh_->ok()) { break; }
      call_req = &request;
      call_resp = &response;

      rt_mutex_take(&pub.nh_->srv_id_mutex_, RT_WAITING_FOREVER);
      call_req->setID(gg_id_++);
      rt_mutex_release(&pub.nh_->srv_id_mutex_);

      if (pub.publish(&request) <= 0) { break; }
      rt_sem_control(&sem_, RT_IPC_CMD_RESET, NULL);
      rt_mutex_release(&mutex_);

      if (rt_sem_take(&sem_, (duration * 1000)) != RT_EOK) {
        ret = false;
        tinyros_log_warn("Service[%s] call_req.id: %u, call timeout!\n", this->topic_.c_str(), call_req->getID());
      } else {
        ret = true;
      }

      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
    } while(0);

    call_req = NULL; call_resp = NULL;
    rt_mutex_release(&mutex_);
    rt_mutex_release(&g_mutex_);
    return ret;
  }

  // these refer to the subscriber
  virtual void callback(unsigned char *data)
  {
    if (call_resp && call_req) {
      rt_mutex_take(&mutex_, RT_WAITING_FOREVER);
      if (call_resp && call_req) {
        uint32_t req_id  = call_req->getID();
        uint32_t resp_id =  ((uint32_t) (*(data + 0)));
        resp_id |= ((uint32_t) (*(data + 1))) << (8 * 1);
        resp_id |= ((uint32_t) (*(data + 2))) << (8 * 2);
        resp_id |= ((uint32_t) (*(data + 3))) << (8 * 3);

        if (req_id == resp_id) {
          call_resp->deserialize(data);
          rt_sem_release(&sem_);
        }
      }
      rt_mutex_release(&mutex_);
    }
  }
  virtual tinyros::string getMsgType()
  {
    return this->resp.getType();
  }
  virtual tinyros::string getMsgMD5()
  {
    return this->resp.getMD5();
  }
  virtual int getEndpointType()
  {
    return tinyros::tinyros_msgs::TopicInfo::ID_SERVICE_CLIENT + tinyros::tinyros_msgs::TopicInfo::ID_SUBSCRIBER;
  }

  virtual bool negotiated()
  { 
    return (negotiated_ && pub.negotiated_); 
  }

  ~ServiceClient() {
    rt_sem_detach(&sem_);
    rt_mutex_detach(&mutex_);
    rt_mutex_detach(&g_mutex_);
  }

  MReq req;
  MRes resp;
  MReq * call_req;
  MRes * call_resp;
  Publisher pub;
  struct rt_semaphore sem_;
  struct rt_mutex mutex_;
  struct rt_mutex g_mutex_;
  static uint32_t gg_id_;
};

template<typename MReq , typename MRes>
uint32_t ServiceClient<MReq , MRes>::gg_id_ = 1;
}
#endif

