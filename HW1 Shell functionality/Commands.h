// Ver: 10-4-2025
#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define PATH_MAX (4096)
#define KB4 (4096)


class Command {
protected:
    std::string m_cmdLine;
    std::vector<std::string> m_argv;
    int m_argc;
    bool m_isBackgroundCommand;
public:
    Command(const std::string cmd_line);

    virtual ~Command();

    virtual void execute() = 0;

    bool getIsBackgroundCommand();

    std::string getCmdLine();

    std::string getCmdLineFull();
    //virtual void prepare();
    //virtual void cleanup();
};

class JobsList {
public:
    class JobEntry {
    public:
        std::string m_jobCommandString;
        pid_t m_jobPID;
        int m_jobID;
        bool m_isStopped;

        JobEntry(std::string commandString, pid_t PID, int ID, bool isStopped) : m_jobCommandString(commandString),
                                                                                 m_jobPID(PID), m_jobID(ID),
                                                                                 m_isStopped(isStopped) {};
    };

private:
    std::map<int, JobEntry> m_jobs;


public:
    int calcNewID();

    JobsList() = default;

    ~JobsList() = default;

    void addJob(Command *cmd, bool isStopped = false, pid_t jobPID = -1); // had to add defult arg to pid_t

    void printJobsList();

    void killAllJobs();

    void removeFinishedJobs();

    JobEntry *getJobById(int jobId); //remember returns nullptr if doesnt exist.

    void removeJobById(int jobId);

    JobEntry *getLastJob(int *lastJobId);

    JobEntry *getLastStoppedJob(int *jobId);

};

class SmallShell {
private:
    std::string m_prompt = "smash";
    JobsList m_jobsList;
    std::string m_lastPWD;
    pid_t m_fgProcPID = -1;
    std::string m_fgCmd;

    SmallShell();

public:
    std::unordered_map<std::string, std::string> m_aliasMap;

    SmallShell(SmallShell const &) = delete; // disable copy ctor
    void operator=(SmallShell const &) = delete; // disable = operator
    static SmallShell &getInstance() // make SmallShell singleton
    {
        static SmallShell instance; // Guaranteed to be destroyed.
        // Instantiated on first use.
        return instance;
    }

    ~SmallShell();

    Command *CreateCommand(const char *cmd_line);

    void executeCommand(const char *cmd_line);

    std::string getPrompt();

    void setPrompt(std::string value);

    std::string getLastPWD();

    void setLastPWD(std::string value);

    pid_t getFgProcPID() const;

    void setFgProcPID(pid_t pid);

    std::string getFgProcCmd() const;

    void setFgProcCmd(std::string cmdLine);

    void clearFgJob();

    JobsList &getJobsList();

    bool isAlias(std::string cmd_line);

    std::string fixAliasCmdLine(std::string cmd_line);


};


class BuiltInCommand : public Command {

public:
    BuiltInCommand(const std::string cmd_line) : Command(cmd_line) {};

    virtual ~BuiltInCommand() {}
};

class ExternalCommand : public Command {
public:
    ExternalCommand(const std::string cmd_line) : Command(cmd_line) {};

    virtual ~ExternalCommand() {}

    void execute() override;
};

////Eitan added ComplexExternalCommand
//class ComplexExternalCommand : public Command {
//public:
//    ComplexExternalCommand(const std::string cmd_line) : Command(cmd_line) {};
//
//    virtual ~ComplexExternalCommand() {}
//
//    void execute() override;
//};
//

//chprompt
class ChPromptCommand : public BuiltInCommand {
public:
    ChPromptCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {}

    virtual ~ChPromptCommand() {}

    void execute() override;
};

//showpid
class ShowPidCommand : public BuiltInCommand {
public:
    ShowPidCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~ShowPidCommand() {}

    void execute() override;
};

//pwd
class GetCurrDirCommand : public BuiltInCommand {
public:
    GetCurrDirCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~GetCurrDirCommand() {}

    void execute() override;
};

//cd
class ChangeDirCommand : public BuiltInCommand {
public:
    std::string m_preChangePWD;

    ChangeDirCommand(const std::string cmd_line) = delete;

    ChangeDirCommand(const std::string cmd_line, std::string plastPwd) : BuiltInCommand(
            cmd_line) { m_preChangePWD = plastPwd; };

    virtual ~ChangeDirCommand() {}

    void execute() override;
};

//jobs
//No direct system calls needed.
//Use internal job list (vector/map with PIDs + metadata)
//waitpid(pid, ...) with WNOHANG to check if jobs are still running
class JobsCommand : public BuiltInCommand {
    JobsList &m_jobsListRef;
public:
    JobsCommand(const std::string cmd_line, JobsList &jobs) : BuiltInCommand(cmd_line), m_jobsListRef(jobs) {};

    virtual ~JobsCommand() {}

    void execute() override;
};

//fg
class ForegroundCommand : public BuiltInCommand {
    JobsList &m_jobsListRef;
public:
    ForegroundCommand(const std::string cmd_line, JobsList &jobs) : BuiltInCommand(cmd_line), m_jobsListRef(jobs) {};

    virtual ~ForegroundCommand() {}

    void execute() override;
};

//quit
class QuitCommand : public BuiltInCommand {
    JobsList &m_jobsListRef;
public:
    QuitCommand(const std::string cmd_line, JobsList &jobs) : BuiltInCommand(cmd_line), m_jobsListRef(jobs) {};

    virtual ~QuitCommand() {}

    void execute() override;
};

//kill
class KillCommand : public BuiltInCommand {
    JobsList &m_jobsListRef;
public:
    KillCommand(const std::string cmd_line, JobsList &jobs) : BuiltInCommand(cmd_line), m_jobsListRef(jobs) {};

    virtual ~KillCommand() {}

    void execute() override;
};

//alias
class AliasCommand : public BuiltInCommand {
public:
    AliasCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~AliasCommand() {
    }

    void execute() override;
};

//unalias
class UnAliasCommand : public BuiltInCommand {
public:
    UnAliasCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~UnAliasCommand() {
    }

    void execute() override;
};

//unsetenv
class UnSetEnvCommand : public BuiltInCommand {
public:
    UnSetEnvCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~UnSetEnvCommand() {
    }

    void execute() override;
};

//watchproc
class WatchProcCommand : public BuiltInCommand {
public:
    WatchProcCommand(const std::string cmd_line) : BuiltInCommand(cmd_line) {};

    virtual ~WatchProcCommand() {
    }

    void execute() override;
};


//Special Commands
class RedirectionCommand : public Command {
    bool m_override;
    std::string m_commandPart;
    std::string m_outPathPart;
public:
    explicit RedirectionCommand(const std::string cmd_line);

    virtual ~RedirectionCommand() {}

    void execute() override;
};

class PipeCommand : public Command {
    std::string m_leftCmd;
    std::string m_rightCmd;
    bool m_toStderr;
public:
    PipeCommand(const std::string cmd_line);

    virtual ~PipeCommand() {}

    void execute() override;
};

class DiskUsageCommand : public Command {
public:
    DiskUsageCommand(const std::string cmd_line) : Command(cmd_line) {};

    virtual ~DiskUsageCommand() {}

    void execute() override;
};

class WhoAmICommand : public Command {
public:
    WhoAmICommand(const std::string cmd_line) : Command(cmd_line) {};

    virtual ~WhoAmICommand() {}

    void execute() override;
};

class NetInfo : public Command {
public:
    NetInfo(const std::string cmd_line) : Command(cmd_line) {};

    virtual ~NetInfo() {}

    void execute() override;
};

#endif //SMASH_COMMAND_H_
