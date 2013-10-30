/* Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/**
  This is unit test for the Global_THD_manager class.
*/

#include "my_config.h"
#include <gtest/gtest.h>
#include "sql_class.h"
#include "thread_utils.h"
#include "mysqld.h"
#include "mysqld_thd_manager.h"  // Global_THD_manager

using thread::Thread;
using thread::Notification;

namespace thd_manager_unittest {

class ThreadManagerTest : public ::testing::Test
{
protected:
  ThreadManagerTest()
  {
  }

  void SetUp()
  {
    Global_THD_manager::create_instance();
    thd_manager= Global_THD_manager::get_instance();
    thd_manager->set_unit_test();
  }

  void TearDown()
  {
  }

  Global_THD_manager *thd_manager;
private:
  GTEST_DISALLOW_COPY_AND_ASSIGN_(ThreadManagerTest);
};

enum TEST_TYPE
{
  TEST_WAIT=0,
  TEST_TIMED_WAIT=1
};

/*
  Verify add_thd(), remove_thd() methods
*/
TEST_F(ThreadManagerTest, AddRemoveTHDWithGuard)
{
  THD thd1(false), thd2(false);
  thd1.server_id= 1;
  thd2.server_id= 2;

  EXPECT_EQ(0U, thd_manager->get_thd_count());
  thd_manager->add_thd(&thd1);
  EXPECT_EQ(1U, thd_manager->get_thd_count());
  thd_manager->add_thd(&thd2);

  thd_manager->remove_thd(&thd1);
  EXPECT_EQ(1U, thd_manager->get_thd_count());
  thd_manager->remove_thd(&thd2);
  EXPECT_EQ(0U, thd_manager->get_thd_count());

}

TEST_F(ThreadManagerTest, IncDecThreadRunning)
{
  EXPECT_EQ(0, thd_manager->get_num_thread_running());
  thd_manager->inc_thread_running();
  EXPECT_EQ(1, thd_manager->get_num_thread_running());
  thd_manager->dec_thread_running();
  EXPECT_EQ(0, thd_manager->get_num_thread_running());
}

TEST_F(ThreadManagerTest, IncThreadCreated)
{
  EXPECT_EQ(0U, thd_manager->get_num_thread_created());
  thd_manager->inc_thread_created();
  EXPECT_EQ(1U, thd_manager->get_num_thread_created());
}

// Verifies death with assert functions
// Google Test recommends DeathTest suffix for classes used in death tests.
typedef ThreadManagerTest ThreadManagerDeathTest;

#if GTEST_HAS_DEATH_TEST && !defined(DBUG_OFF) && SAFE_MUTEX
TEST_F(ThreadManagerDeathTest, AssertTest)
{
  thd_manager->acquire_thd_lock();
  ::testing::FLAGS_gtest_death_test_style= "threadsafe";
  EXPECT_DEATH(thd_manager->assert_if_mutex_owner(),
               ".*Assertion.*LOCK_thd_count*");
  thd_manager->release_thd_lock();
  EXPECT_DEATH(thd_manager->assert_if_not_mutex_owner(),
               ".*Assertion.*LOCK_thd_count*");
}
#endif

// Inverse of assert functionality is checked
TEST_F(ThreadManagerTest, AssertOwnerTest)
{
  // Should not assert as mutex is not acquired
  thd_manager->assert_if_mutex_owner();
  thd_manager->acquire_thd_lock();
  thd_manager->assert_if_not_mutex_owner();
  thd_manager->release_thd_lock();
  thd_manager->assert_if_mutex_owner();
}

/*
  Test function to validate do_for_all_thd, do_for_all_thd_copy.
  It emulates counter function to count number of thds in thd list.
*/
class TestFunc1 : public Do_THD_Impl
{
private:
  int cnt;
public:
  TestFunc1() : cnt(0) {}
  int get_count()
  {
    return cnt;
  }
  void reset_count()
  {
    cnt= 0;
  }
  void operator() (THD* thd)
  {
    cnt= cnt + 1;
  }
};

TEST_F(ThreadManagerTest, TestTHDCopyDoFunc)
{
  THD thd1(false), thd2(false);
  thd1.server_id= 1;
  thd2.server_id= 2;
  // Add two THD into thd list.
  thd_manager->add_thd(&thd1);
  thd_manager->add_thd(&thd2);

  int cnt= 0;
  TestFunc1 testFunc1;
  thd_manager->do_for_all_thd_copy(&testFunc1);
  cnt= testFunc1.get_count();
  EXPECT_EQ(2, cnt);

  testFunc1.reset_count();
  thd_manager->do_for_all_thd(&testFunc1);
  cnt= testFunc1.get_count();
  EXPECT_EQ(2, cnt);

  // Cleanup - Remove added THD.
  thd_manager->remove_thd(&thd1);
  thd_manager->remove_thd(&thd2);
}

/*
  Test class to verify find_thd()
*/
class TestFunc2 : public Find_THD_Impl
{
public:
  TestFunc2() : search_value(0)  {}
  bool operator() (THD* thd)
  {
    if (thd->server_id == search_value)
    {
      return true;
    }
    return false;
  }
  void set_search_value(uint val) { search_value= val; }
private:
  uint search_value;
};

/*
  Test class to verify do_all_for_thd() function.
  Counts all thd whose server_id value is less than or equal to 2.
*/
class TestFunc3 : public Do_THD_Impl
{
public:
  TestFunc3() : count(0) {}
  void operator() (THD* thd)
  {
    if (thd->server_id <= 2)
    {
      count++;
    }
  }
  int get_count() { return count; }
private:
  int count;
};

TEST_F(ThreadManagerTest, TestTHDFindFunc)
{
  THD thd1(false), thd2(false);
  thd1.server_id= 1;
  thd2.server_id= 2;
  thd_manager->add_thd(&thd1);
  thd_manager->add_thd(&thd2);
  TestFunc2 testFunc2;
  testFunc2.set_search_value(2);
  THD *thd= thd_manager->find_thd(&testFunc2);
  /* Returns the last thd which matches. */
  EXPECT_EQ(2U, thd->server_id);

  testFunc2.set_search_value(6);
  thd= thd_manager->find_thd(&testFunc2);
  /* Find non existing thd with server_id value 6. Expected to return NULL. */
  const THD* null_thd= NULL;
  EXPECT_EQ(null_thd, thd);

  // Cleanup - Remove added THD.
  thd_manager->remove_thd(&thd1);
  thd_manager->remove_thd(&thd2);
}


TEST_F(ThreadManagerTest, TestTHDCountFunc)
{
  THD thd1(false), thd2(false), thd3(false);
  thd1.server_id= 1;
  thd2.server_id= 2;
  thd3.server_id= 3;
  thd_manager->add_thd(&thd1);
  thd_manager->add_thd(&thd2);
  thd_manager->add_thd(&thd3);

  TestFunc3 testFunc3;
  thd_manager->do_for_all_thd(&testFunc3);
  int ret=testFunc3.get_count();
  // testFunc3 counts for thd->server_id values, 1 and 2.
  EXPECT_EQ(2, ret);

  // Cleanup - Remove added THD.
  thd_manager->remove_thd(&thd1);
  thd_manager->remove_thd(&thd2);
  thd_manager->remove_thd(&thd3);
}

}  // namespace