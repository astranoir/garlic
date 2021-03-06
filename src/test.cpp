/**
 * This Source Code Form is subject to the terms 
 * of the Mozilla Public License, v. 2.0. If a 
 * copy of the MPL was not distributed with this 
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <onion/log.h>
#include <unistd.h>
#include <assert.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include "test.hpp"

using namespace Garlic;

Test::Test(IniReader &reader) : ini(reader)
{
	
}

bool Test::check_and_run()
{
	if (check()){
		char now[32];
		snprintf(now,sizeof(now),"%ld",time(NULL));
		run(0, default_rw_env(), now);
		return true;
	}
	return false;
}

bool Test::check()
{
	if (ini.has("scripts.check")){
		setup_env(default_rw_env());
		int ok=system(ini.get("scripts.check").c_str());
		return ok!=0;
	}
	return 1; // No test, run always.
}


int Test::run(int test_id,  const std::map<std::string, std::string> &extra_env, const std::string &testname)
{
	setup_env(extra_env);
	char tmp[1024];
	const std::string logpath=ini.get("logpath", ini.getPath()+ "/log/");

	ONION_DEBUG("Log path is %s", logpath.c_str());
	{
		int err_mkdir=mkdir(logpath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
		if (err_mkdir <0 && err_mkdir != -EEXIST){
			//ONION_DEBUG("Error creating log path %s: %s", logpath.c_str(), strerror(errno));
		}
	}
	std::string basefilename=logpath+testname;
	
	ONION_DEBUG("Debug to %s",(basefilename+".[pid,output,result]").c_str());
	ONION_DEBUG("Output to %s",(basefilename+".output").c_str());

	int fd;
	fd=open((basefilename+".pid").c_str(), O_WRONLY|O_CREAT, 0666);
	if (fd<0)
		perror("Cant open pid file");
	assert(fd>=0);
	snprintf(tmp,sizeof(tmp),"%d",getpid());
	int w=write(fd,tmp,strlen(tmp));
	assert(w==(signed)strlen(tmp));
	close(fd);
	
	int fd_stderr=dup(2);
	int fd_stdout=dup(1);
	int fd_stdin=dup(0);
	int ok;
	
	{ // Run of the test command, close all fds, open output to result file, close all at the end.
		close(0);
		close(1);
		close(2);

		fd=open("/dev/null", O_RDONLY);
		assert(fd==0);
		fd=open( (basefilename+".output").c_str(), O_WRONLY|O_CREAT, 0666);
		assert(fd==1);
		fd=dup2(1,2);
		assert(fd==2);
		
		if (test_id==0)
			ok=system(ini.get("scripts.test","./test.sh").c_str());
		else
			ok=system(ini.get("scripts.test_"+std::to_string(test_id),"./test.sh").c_str());
		
		
// 		fd=close(0);
// 		assert(fd==0);
// 		fd=close(1);
// 		assert(fd==0);
// 		fd=close(2);
// 		assert(fd==0);
	}

	fd=dup2(fd_stderr,2);
	assert(fd==2);
	close(fd_stderr);

	fd=dup2(fd_stdout,1);
	assert(fd==1);
	close(fd_stdout);

	fd=dup2(fd_stdin,0);
	assert(fd==0);
	close(fd_stdin);
	
	ONION_DEBUG("Test finished. Result %d", ok);
	
	snprintf(tmp,16,"%d",ok);
	fd=open((basefilename+".result").c_str(), O_WRONLY|O_CREAT, 0666);
	w=write(fd,tmp,strlen(tmp));
	assert(w==(signed)strlen(tmp));
	close(fd);

	{ // Do something with the results.
		std::string error_on_last_file(logpath+"/error_on_last");
		fd=close(0); // Close just in case
		fd=open((basefilename+".output").c_str(), O_RDONLY);
		assert(fd==0);
		if (ok!=0){
			ONION_DEBUG("Error running test");
			if (ini.has("scripts.on_error")){
				ONION_DEBUG("Execute: %s", ini.get("scripts.on_error").c_str());
				int ok=system(ini.get("scripts.on_error").c_str());
				if (ok!=0){
					ONION_ERROR("Coult not execute on_error script");
				}
			}
			fd=open(error_on_last_file.c_str(),O_WRONLY|O_CREAT, 0666);
			assert(fd>=0);
			close(fd);
		}
		else if (access(error_on_last_file.c_str(), F_OK)!=-1){
			ONION_DEBUG("Success after many errors!");
			if (ini.has("scripts.on_error")){
				ONION_DEBUG("Execute: %s", ini.get("scripts.on_back_to_normal").c_str());
				ok=system(ini.get("scripts.on_back_to_normal").c_str());
				if (ok!=0){
					ONION_ERROR("Error executing script on_back_to_normal");
				}
			}
			unlink(error_on_last_file.c_str());
		}
		fd=close(0);
		assert(fd==0);
	}
	
	return ok;
}

std::map< std::string, std::string > Test::default_rw_env(){
	std::map<std::string, std::string> extra_env;
	if (ini.has("env-rw"))
		for(auto &str: ini.get_keys("env-rw")){
			extra_env[str]=ini.get("env-rw."+str);
		}
	return extra_env;
}

void Test::setup_env(const std::map<std::string, std::string> &extra_env){
	int ok=0;
	ok=chdir(ini.getPath().c_str());
	ONION_DEBUG("Chdir to %s", ini.getPath().c_str());
	if (ini.has("scripts.chdir")){
		ok=chdir(ini.get("scripts.chdir").c_str());
		ONION_DEBUG("Chdir to %s", ini.get("scripts.chdir").c_str());
	}
	if (ok<0){
	  ONION_ERROR("Could not chdir to <%s> + <%s>: %s", ini.getPath().c_str(), ini.get("scripts.chdir").c_str(), strerror(errno));
		return;
	}
	
	if (ini.has("env")){
		for(const auto &k:ini.get_keys("env")){
			ONION_DEBUG("Set env %s=%s",k.c_str(), ini.get("env."+k).c_str());
			setenv(k.c_str(), ini.get("env."+k).c_str(), 1);
		}
	}
	if (extra_env.size()>0){
		for(auto &pair: extra_env){
			ONION_DEBUG("Set env %s=%s",pair.first.c_str(), pair.second.c_str());
			setenv(pair.first.c_str(), pair.second.c_str(), 1);
		}
	}
}

