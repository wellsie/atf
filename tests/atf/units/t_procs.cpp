//
// Automated Testing Framework (atf)
//
// Copyright (c) 2008 The NetBSD Foundation, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. All advertising materials mentioning features or use of this
//    software must display the following acknowledgement:
//        This product includes software developed by the NetBSD
//        Foundation, Inc. and its contributors.
// 4. Neither the name of The NetBSD Foundation nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
// IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

extern "C" {
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

#include "atf/exceptions.hpp"
#include "atf/io.hpp"
#include "atf/macros.hpp"
#include "atf/procs.hpp"
#include "atf/sanity.hpp"
#include "atf/signals.hpp"
#include "atf/text.hpp"
#include "atf/utils.hpp"

// ------------------------------------------------------------------------
// Auxiliary functions.
// ------------------------------------------------------------------------

static
size_t
count_nodes(const size_t degree, const size_t height)
{
    size_t nnodes = 1, lastlevel = 1;
    for (size_t h = 1; h <= height; h++) {
        nnodes += lastlevel * degree;
        lastlevel *= degree;
    }
    return nnodes;
}

atf::procs::pid_set my_children;

void
sigterm_handler(int signo)
{
    using atf::procs::pid_set;

    for (pid_set::const_iterator iter = my_children.begin();
         iter != my_children.end(); iter++) {
        int status;
        std::cout << ::getpid() << " waiting for " << *iter << std::endl;
        ::waitpid(*iter, &status, 0);
    }

    std::exit(EXIT_SUCCESS);
}

static
void
do_spawn(const size_t degree, const size_t height, size_t level,
         size_t pos, atf::io::pipe* pipes)
{
    PRE(level > 0 && level <= height);

    my_children.clear();

    for (size_t d = 0; d < degree; d++) {
        pid_t pid = ::fork();
        if (pid == 0) {
            size_t nodeid = count_nodes(degree, level - 1) + pos * degree + d;

            std::cout << std::string(level - 1, ' ') << "Pid " << ::getpid()
                      << ", id " << nodeid << std::endl;

            pipes[nodeid - 1].rend().close();
            std::string idstr = atf::text::to_string(::getpid());
            ::write(pipes[nodeid - 1].wend().get(), idstr.c_str(),
                    idstr.length());

            if (level < height)
                do_spawn(degree, height, level + 1, d + pos * degree, pipes);

            std::exit(EXIT_SUCCESS);
        } else if (pid > 0)
            my_children.insert(pid);
    }

    if (level > 1) {
        atf::signals::signal_programmer sp(SIGTERM, sigterm_handler);
        for (;;)
            sleep(30);
    }
}

static
atf::procs::pid_set
spawn_tree(size_t degree, size_t height)
{
    size_t nnodes = count_nodes(degree, height);

    atf::utils::auto_array< atf::io::pipe > pipes(new atf::io::pipe[nnodes]);

    std::cout << "Spawning process tree of degree " << degree << " and "
              << "height " << height << ", " << nnodes << " total nodes"
              << std::endl;
    do_spawn(degree, height, 1, 0, pipes.get());

    atf::procs::pid_set pids;
    for (size_t i = 0; i < nnodes - 1; i++) {
        pipes[i].wend().close();
        char buf[1024];
        int cnt;
        while ((cnt = ::read(pipes[i].rend().get(), buf, sizeof(buf))) <= 0)
            ::usleep(10000);
        buf[cnt] = '\0';
        pids.insert(atf::text::to_type< pid_t >(buf));
    }

    std::cout << "PIDs of spawned children:";
    for (atf::procs::pid_set::const_iterator iter = pids.begin();
         iter != pids.end(); iter++)
        std::cout << " " << *iter;
    std::cout << std::endl;

    POST(pids.size() == nnodes - 1);
    return pids;
}

// ------------------------------------------------------------------------
// Tests for the "pid_grabber" class.
// ------------------------------------------------------------------------

ATF_TEST_CASE(pid_grabber_get_children_of);
ATF_TEST_CASE_HEAD(pid_grabber_get_children_of)
{
    set("descr", "Checks that pid_grabber's get_children_of method "
                 "correctly returns the list of children PIDs for a "
                 "given process");
}
ATF_TEST_CASE_BODY(pid_grabber_get_children_of)
{
    using atf::procs::pid_grabber;
    using atf::procs::pid_set;

    pid_grabber pg;
    if (!pg.can_get_children_of())
        ATF_SKIP("Unsupported platform: cannot get list of children");

    const size_t degree = 3;
    const size_t height = 1; // Must be 1 for this specific test.
    pid_set pids = spawn_tree(degree, height);

    pid_set pids2 = pg.get_children_of(::getpid());
    std::cout << "PIDs of grabbed children:";
    for (pid_set::const_iterator iter = pids2.begin(); iter != pids2.end();
         iter++)
        std::cout << " " << *iter;
    std::cout << std::endl;

    if (pids != pids2)
        ATF_FAIL("Lists of children do not match");

    for (int i = 0; i < degree; i++) {
        int status;
        if (::wait(&status) == -1)
            throw atf::system_error("pid_grabber_get_children_of",
                                    "wait failed", errno);
    }
}

// ------------------------------------------------------------------------
// Tests for free functions.
// ------------------------------------------------------------------------

static
void
one_kill_tree(size_t degree, size_t height, atf::procs::pid_grabber& pg)
{
    using atf::procs::pid_grabber;
    using atf::procs::pid_set;

    pid_set pids = spawn_tree(degree, height);

    pid_set children = pg.get_children_of(::getpid());
    for (pid_set::const_iterator iter = children.begin();
         iter != children.end(); iter++) {
        std::cout << "Killing tree starting at PID " << *iter << std::endl;
        using atf::procs::errors_vector;
        using atf::procs::kill_tree;
        using atf::procs::pid_error_pair;;

        errors_vector errs = kill_tree(*iter, SIGTERM, pg);
        if (!errs.empty()) {
            for (errors_vector::const_iterator iter2 = errs.begin();
                 iter2 != errs.end(); iter2++) {
                pid_error_pair p = *iter2;
                std::cout << "Error: PID " << p.first << ": " << p.second
                          << std::endl;
            }
            ATF_FAIL("kill_tree reported errors");
        }
    }

    for (int i = 0; i < degree; i++) {
        int status;
        if (::wait(&status) == -1)
            throw atf::system_error("kill_tree",
                                    "wait failed", errno);
    }

    bool failed = false;
    for (pid_set::const_iterator iter = pids.begin(); iter != pids.end();
         iter++) {
        if (::kill(*iter, SIGKILL) != -1 || errno != ESRCH) {
            failed = true;
            std::cout << "Process " << *iter << " was not killed"
                      << std::endl;
        }
    }
    if (failed)
        ATF_FAIL("Some processes were not killed");
}

static
void
torture_kill_tree(size_t degree, size_t height, size_t rounds)
{
    using atf::procs::pid_grabber;
    using atf::procs::pid_set;

    pid_grabber pg;
    if (!pg.can_get_children_of())
        ATF_SKIP("Unsupported platform: cannot get list of children");

    // The kill_tree algorithm is highly subject to race conditions, so
    // torture-test it in the hope that easy-to-trigger ones will pop up.
    for (size_t i = 0; i < rounds; i++) {
        std::cerr << "Starting kill_tree, degree " << degree
                  << ", height " << height << ", round " << i << std::endl;
        one_kill_tree(degree, height, pg);
    }
}

ATF_TEST_CASE(kill_tree_once);
ATF_TEST_CASE_HEAD(kill_tree_once)
{
    set("descr", "Checks that the kill_tree correctly kills a tree of "
                 "processes");
}
ATF_TEST_CASE_BODY(kill_tree_once)
{
    torture_kill_tree(3, 3, 1);
}

ATF_TEST_CASE(kill_tree_torture);
ATF_TEST_CASE_HEAD(kill_tree_torture)
{
    set("descr", "Checks that the kill_tree correctly kills a tree of "
                 "processes and ensures there are no trivial race "
                 "conditions");
}
ATF_TEST_CASE_BODY(kill_tree_torture)
{
    for (size_t degree = 1; degree < 4; degree++)
        for (size_t height = 1; height < 4; height++)
            torture_kill_tree(degree, height, 100);
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the tests for the "pid_grabber" class.
    ATF_ADD_TEST_CASE(tcs, pid_grabber_get_children_of);

    // Add the tests for the free functions.
    ATF_ADD_TEST_CASE(tcs, kill_tree_once);
    ATF_ADD_TEST_CASE(tcs, kill_tree_torture);
}
