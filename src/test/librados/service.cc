#include "include/rados/librados.h"
#include "include/rados/librados.hpp"
#include "include/stringify.h"
#include "common/config_proxy.h"
#include "test/librados/test.h"
#include "test/librados/TestCase.h"

#include <algorithm>
#include <thread>
#include <errno.h>
#include "gtest/gtest.h"
#include "test/unit.cc"

using namespace librados;

TEST(LibRadosService, RegisterEarly) {
  rados_t cluster;
  ASSERT_EQ(0, rados_create(&cluster, "admin"));
  ASSERT_EQ(0, rados_conf_read_file(cluster, NULL));
  ASSERT_EQ(0, rados_conf_parse_env(cluster, NULL));

  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(0, rados_service_register(cluster, "laundry", name.c_str(),
                                      "foo\0bar\0this\0that\0"));
  ASSERT_EQ(-EEXIST, rados_service_register(cluster, "laundry", name.c_str(),
                                            "foo\0bar\0this\0that\0"));

  ASSERT_EQ(0, rados_connect(cluster));
  sleep(5);
  rados_shutdown(cluster);
}

TEST(LibRadosService, RegisterLate) {
  rados_t cluster;
  ASSERT_EQ(0, rados_create(&cluster, "admin"));
  ASSERT_EQ(0, rados_conf_read_file(cluster, NULL));
  ASSERT_EQ(0, rados_conf_parse_env(cluster, NULL));
  ASSERT_EQ(0, rados_connect(cluster));

  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(0, rados_service_register(cluster, "laundry", name.c_str(),
                                      "foo\0bar\0this\0that\0"));
  ASSERT_EQ(-EEXIST, rados_service_register(cluster, "laundry", name.c_str(),
                                            "foo\0bar\0this\0that\0"));
  rados_shutdown(cluster);
}

TEST(LibRadosService, Status) {
  rados_t cluster;
  ASSERT_EQ(0, rados_create(&cluster, "admin"));
  ASSERT_EQ(0, rados_conf_read_file(cluster, NULL));
  ASSERT_EQ(0, rados_conf_parse_env(cluster, NULL));

  ASSERT_EQ(-ENOTCONN, rados_service_update_status(cluster,
                                                   "testing\0testing\0"));

  ASSERT_EQ(0, rados_connect(cluster));
  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(0, rados_service_register(cluster, "laundry", name.c_str(),
                                      "foo\0bar\0this\0that\0"));

  for (int i=0; i<20; ++i) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s%c%s%c%s%c%d%c",
             "testing", '\0', "testing", '\0',
             "count", '\0', i, '\0');
    ASSERT_EQ(0, rados_service_update_status(cluster, buffer));
    sleep(1);
  }
  rados_shutdown(cluster);
}

TEST(LibRadosServicePP, RegisterEarly) {
  Rados cluster;
  cluster.init("admin");
  ASSERT_EQ(0, cluster.conf_read_file(NULL));
  cluster.conf_parse_env(NULL);
  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(0, cluster.service_daemon_register(
	      "laundry", name, {{"foo", "bar"}, {"this", "that"}}));
  ASSERT_EQ(-EEXIST, cluster.service_daemon_register(
	      "laundry", name, {{"foo", "bar"}, {"this", "that"}}));
  ASSERT_EQ(0, cluster.connect());
  sleep(5);
  cluster.shutdown();
}

TEST(LibRadosServicePP, RegisterLate) {
  Rados cluster;
  cluster.init("admin");
  ASSERT_EQ(0, cluster.conf_read_file(NULL));
  cluster.conf_parse_env(NULL);
  ASSERT_EQ("", connect_cluster_pp(cluster));
  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(0, cluster.service_daemon_register(
	      "laundry", name, {{"foo", "bar"}, {"this", "that"}}));
}

TEST(LibRadosServicePP, Status) {
  Rados cluster;
  cluster.init("admin");
  ASSERT_EQ(0, cluster.conf_read_file(NULL));
  cluster.conf_parse_env(NULL);
  string name = string("pid") + stringify(getpid());
  ASSERT_EQ(-ENOTCONN, cluster.service_daemon_update_status(
	      {{"testing", "starting"}}));
  ASSERT_EQ(0, cluster.connect());
  ASSERT_EQ(0, cluster.service_daemon_register(
	      "laundry", name, {{"foo", "bar"}, {"this", "that"}}));
  for (int i=0; i<20; ++i) {
    ASSERT_EQ(0, cluster.service_daemon_update_status({
	  {"testing", "running"},
	  {"count", stringify(i)}
	}));
    sleep(1);
  }
  cluster.shutdown();
}

TEST(LibRadosServicePP, Close) {
  int tries = 20;
  string name = string("close-test-pid") + stringify(getpid());
  int i;
  for (i = 0; i < tries; ++i) {
    cout << "attempt " << i << " of " << tries << std::endl;
    {
      Rados cluster;
      cluster.init("admin");
      ASSERT_EQ(0, cluster.conf_read_file(NULL));
      cluster.conf_parse_env(NULL);
      ASSERT_EQ(0, cluster.connect());
      ASSERT_EQ(0, cluster.service_daemon_register(
		  "laundry", name, {{"foo", "bar"}, {"this", "that"}}));
      sleep(3); // let it register
      cluster.shutdown();
    }
    // mgr updates servicemap every tick
    //sleep(g_conf().get_val<int64_t>("mgr_tick_period"));
    std::this_thread::sleep_for(g_conf().get_val<std::chrono::seconds>(
				  "mgr_tick_period"));
    // make sure we are deregistered
    {
      Rados cluster;
      cluster.init("admin");
      ASSERT_EQ(0, cluster.conf_read_file(NULL));
      cluster.conf_parse_env(NULL);
      ASSERT_EQ(0, cluster.connect());
      bufferlist inbl, outbl;
      ASSERT_EQ(0, cluster.mon_command("{\"prefix\": \"service dump\"}",
				       inbl, &outbl, NULL));
      string s = outbl.to_str();
      cluster.shutdown();

      if (s.find(name) != string::npos) {
	cout << " failed to deregister:\n" << s << std::endl;
      } else {
	break;
      }
    }
  }
  ASSERT_LT(i, tries);
}
