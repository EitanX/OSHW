#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <iomanip>
#include "Commands.h"
#include <fcntl.h>
#include <unordered_set>

#include <net/if.h>
#include <cerrno>
#include <iomanip>
#include <cstring>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

//--------------------OWN HELPERS--------------------//
#pragma region OWN HELPERS

void printError(std::string sysCallName) {
    std::string errorText = "smash error: " + sysCallName + " failed";
    perror(errorText.c_str());
}

std::string readFile(const std::string path) {
    char buffer[KB4] = {0};
    //OPEN FILE
    int fd = syscall(SYS_open, path.c_str(), 0);
    if (fd == -1) {
        printError("open");
        return "";
    }
    //READ FILE
    long bytesRead = syscall(SYS_read, fd, buffer, sizeof(buffer) - 1);
    if (bytesRead <= 0) {
        printError("read");
        if (syscall(SYS_close, fd) == -1) printError("close");
        return "";
    }
    buffer[bytesRead] = '\0';
    if (syscall(SYS_close, fd) == -1) {
        printError("close");
        return "";
    }

    return std::string(buffer, bytesRead);
}

//TODO: maybe move the __environ inside the functions.
extern char **__environ;

bool envVarExists(const std::string &name) {
    int pid = syscall(SYS_getpid);
    std::string path = "/proc/" + std::to_string(pid) + "/environ";

    std::string buffer = readFile(path);
    if (buffer.empty()) return false;

    size_t pos = 0;
    while (pos < buffer.size()) {
        size_t end = buffer.find('\0', pos);
        if (end == std::string::npos) break;

        std::string entry = buffer.substr(pos, end - pos);

        // split to key and value by first '='
        size_t equalPos = entry.find('=');
        if (equalPos != std::string::npos) {
            std::string key = entry.substr(0, equalPos);
            if (key == name) return true;
        }

        pos = end + 1;
    }

    return false;
}

bool removeEnvVar(const std::string &name) {
    for (int i = 0; __environ[i]; ++i) {
        std::string entry(__environ[i]);
        if (entry.rfind(name + "=", 0) == 0) {
            for (; __environ[i]; ++i) {
                __environ[i] = __environ[i + 1];
            }
            return true;
        }
    }
    return false;
}

struct linuxDirectoryEntry {
    unsigned long int m_inodeNumber;
    long int m_offsetToNextEntry;
    unsigned short m_recordLength;
    unsigned char m_fileType;
    char m_fileName[];
};

long recursiveFolderSizeCalc(const std::string &path, const bool isBasePath = false) {
    long totalSize = 0;
    char buffer[KB4];

    int fd = syscall(SYS_open, path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1) {
        printError("open");
        return 0;
    }

    long bytesRead;
    while ((bytesRead = syscall(SYS_getdents64, fd, buffer, sizeof(buffer))) > 0) {
        int offset = 0;
        while (offset < bytesRead) {
            linuxDirectoryEntry *entry = (linuxDirectoryEntry *) (buffer + offset);
            std::string name(entry->m_fileName);

            if (isBasePath && name == ".") {
                std::string fullPath = path + "/" + name;
                struct stat st;
                if (syscall(SYS_lstat, fullPath.c_str(), &st) == -1) {
                    printError("lstat");
                    offset += entry->m_recordLength;
                    continue;
                }

                totalSize += (st.st_blocks + 1) * 512 / 1024;
                offset += entry->m_recordLength;
                continue;
            }

            if (name == "." || name == "..") {
                offset += entry->m_recordLength;
                continue;
            }

            std::string fullPath = path + "/" + name;
            struct stat st;
            if (syscall(SYS_lstat, fullPath.c_str(), &st) == -1) {
                printError("lstat");
                offset += entry->m_recordLength;
                continue;
            }

            totalSize += (st.st_blocks + 1) * 512 / 1024;

            if (S_ISDIR(st.st_mode)) {
                totalSize += recursiveFolderSizeCalc(fullPath);
            }

            offset += entry->m_recordLength;
        }
    }

    if (bytesRead == -1) {
        printError("getdents64");
    }

    syscall(SYS_close, fd);
    return totalSize;
}


/* ---------- IP & Netmask ---------- */
static bool getIfaceAddr(const std::string &iface,
                         std::string &ip, std::string &mask) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        printError("socket");
        return false;
    }

    ifreq ifr{};
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.c_str());

    /* IP */
    if (ioctl(s, SIOCGIFADDR, &ifr) == -1) {
        close(s);
        return false;
    }
    ip = inet_ntoa(((sockaddr_in *) &ifr.ifr_addr)->sin_addr);

    /* Netmask */
    if (ioctl(s, SIOCGIFNETMASK, &ifr) == -1) {
        close(s);
        return false;
    }
    mask = inet_ntoa(((sockaddr_in *) &ifr.ifr_netmask)->sin_addr);

    close(s);
    return true;
}

/* ---------- Gateway (/proc/net/route) ---------- */

static std::string getDefaultGateway(const std::string &iface) {
    std::string file = readFile("/proc/net/route");
    std::istringstream in(file);
    std::string line;
    std::getline(in, line);

    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string ifname;
        unsigned int dest = 0, gw = 0, flags = 0;

        ls >> ifname >> std::hex >> dest >> gw >> flags;
        if (ifname != iface || dest != 0 || !(flags & 0x2))
            continue;

        /* --- פעם אחת: Little → Big --- */
        uint32_t ip_net = htonl(gw);

        /* --- בניית‑מחרוזת ידנית --- */
        std::ostringstream out;
        out << ((ip_net >> 24) & 0xFF) << '.'
            << ((ip_net >> 16) & 0xFF) << '.'
            << ((ip_net >> 8) & 0xFF) << '.'
            << (ip_net & 0xFF);

        return out.str();
    }
    return "";
}

/* ---------- DNSServers (/etc/resolv.conf) ---------- */
static std::vector<std::string> getDnsServers() {
    std::vector<std::string> v;
    std::string data = readFile("/etc/resolv.conf");
    std::istringstream in(data);
    std::string word;
    while (in >> word) {
        if (word == "nameserver") {
            std::string ip;
            in >> ip;
            v.push_back(ip);
        }
        std::getline(in, word);
    }
    return v;
}

static bool parseUtimeStime(const std::string &statLine,
                            unsigned long long &utime,
                            unsigned long long &stime) {
    size_t rp = statLine.find(')');
    if (rp == std::string::npos) return false;

    std::istringstream in(statLine.substr(rp + 2));

    std::string tok;
    for (int i = 0; i < 11; ++i) in >> tok;

    in >> utime >> stime;
    return !in.fail();
}

static bool parseTotalCpuTicks(const std::string &statContent,
                               unsigned long long &total) {
    std::istringstream in(statContent);
    std::string label;
    unsigned long long val = 0, sum = 0;

    if (!(in >> label) || label != "cpu")
        return false;

    while (in >> val) sum += val;
    total = sum;
    return true;
}

#pragma endregion

//--------------------GIVEN HELPERS--------------------//
#pragma region GIVEN HELPERS

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;

    FUNC_EXIT()
}

int parseCommandLine(const std::string &cmd_line, std::vector<std::string> *argsVector) {
    FUNC_ENTRY();
    int i = 0;
    std::istringstream iss(_trim(cmd_line));
    for (std::string token; iss >> token;) {
        argsVector->push_back(token);
        i++;
    }
    return i;

    FUNC_EXIT();
}

bool _isBackgroundCommand(const char *cmd_line) {
    const string str(cmd_line);
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

bool isBackgroundCommand(const std::string cmd_line) {
    return cmd_line[cmd_line.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    unsigned int idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

std::string removeBackgroundSign(const std::string cmd_line) {
    std::string result = cmd_line;
    result = _trim(result);

    size_t idx = result.find_last_not_of(WHITESPACE);
    if (idx == std::string::npos || result[idx] != '&') {
        return result;  // unchanged
    }

    // Remove '&' and trim trailing whitespace
    result[idx] = ' ';
    size_t newEnd = result.find_last_not_of(WHITESPACE, idx);
    return result.substr(0, newEnd + 1);
}

#pragma endregion

//--------------------COMMAND::EXECUTE()--------------------//
#pragma region COMMAND::EXECUTE()

void ChPromptCommand::execute() {
    if (m_argc == 1) {
        SmallShell::getInstance().setPrompt("smash");
        return;
    }
    SmallShell::getInstance().setPrompt(m_argv[1]);
}

void ShowPidCommand::execute() {
    pid_t pid = syscall(SYS_getpid);
    cout << "smash pid is " << pid << endl;
}

void GetCurrDirCommand::execute() {
    char cwd[PATH_MAX];
    if (syscall(SYS_getcwd, cwd, PATH_MAX) == -1) {
        printError("getcwd");
        return;
    }
    std::cout << cwd << std::endl;
}

void ChangeDirCommand::execute() {
    if (m_argc == 1) {
        return;
    }
    if (m_argc > 2) {
        std::cerr << "smash error: cd: too many arguments" << std::endl;
        return;
    }
    std::string newPath;
    if (m_argv[1] == "-") {
        if (SmallShell::getInstance().getLastPWD() == "") {
            std::cerr << "smash error: cd: OLDPWD not set" << std::endl;
            return;
        }
        newPath = SmallShell::getInstance().getLastPWD();
    } else if (m_argv[1][0] == '/') {
        newPath = m_argv[1];
    } else {
        char cwd[PATH_MAX];
        if (syscall(SYS_getcwd, cwd, PATH_MAX) == -1) {
            printError("getcwd");
            return;
        }
        newPath = std::string(cwd) + "/" + m_argv[1];
    }
    if (syscall(SYS_chdir, newPath.c_str()) == -1) {
        printError("chdir");
        return;
    }
    SmallShell::getInstance().setLastPWD(newPath);
}

void JobsCommand::execute() {
    m_jobsListRef.printJobsList();
}

void ForegroundCommand::execute() {
    int jobId;
    JobsList::JobEntry *job = this->m_jobsListRef.getLastJob(&jobId);

    //ERROR HANDLING + VALUE EXTRACTION
    //taking care of no args case error, if JobsList is empty we get nullptr from getLastJob(&jobId)
    if (this->m_argc == 1 && job == nullptr) {
        std::cerr << "smash error: fg: jobs list is empty" << std::endl;
        return;
    }
    //if we have arguments we need to fix the values to the correct ones. ELSE WE ALREADY GOT THEM!
    if (this->m_argc != 1) {
        //taking care of too many arguments.
        if (this->m_argc > 2) {
            std::cerr << "smash error: fg: invalid arguments" << std::endl;
            return;
        }
        //here we get for sure 2 arguments - extracting the jobId StoI
        try {
            jobId = stoi((this->m_argv[1]));
        } catch (const std::invalid_argument &error) {
            std::cerr << "smash error: fg: invalid arguments" << std::endl;
            return;
        }
        //here we extracted a jobId successfully
        job = this->m_jobsListRef.getJobById(jobId);
        if (job == nullptr) {
            std::cerr << "smash error: fg: job-id " << jobId << " does not exist" << std::endl;
            return;
        }
    }

    //WE GOT CORRECT VALUES FOR THE JOB! -----> COMMAND LOGIC
    pid_t pid = job->m_jobPID;
    SmallShell::getInstance().setFgProcPID(pid);
    std::cout << job->m_jobCommandString << " " << pid << std::endl;

    // const pid_t smashPID = syscall(SYS_getpid);
    // //give terminal control to the job
    // syscall(SYS_tcsetpgrp, STDIN_FILENO, pid);
    //send SIGCONT in case process is stopped
    // if (syscall(SYS_kill, pid, SIGCONT) == -1) printError("kill");
    // //wait process to finish
    // int status;
    // if (syscall(SYS_wait4, pid, &status, 0, nullptr) == -1) printError("waitpid");
    // //after process in finished - return terminal control to the shell
    // syscall(SYS_tcsetpgrp, STDIN_FILENO, smashPID);

//    if (syscall(SYS_wait4, pid, nullptr, 0, nullptr) == -1) printError("waitpid");
    long result = syscall(SYS_wait4, pid, nullptr, 0, nullptr);
    if (result == -1) {
        if (errno != ECHILD) {
            printError("waitpid");
        }
    }
    SmallShell::getInstance().setFgProcPID(-1);
    this->m_jobsListRef.removeJobById(jobId);
}

void QuitCommand::execute() {
    bool withKill = (m_argc >= 2) && (std::string(m_argv[1]) == "kill");
    if (withKill) {
        m_jobsListRef.killAllJobs();
    }
    m_jobsListRef.removeFinishedJobs();
    //maybe free memory?
    syscall(SYS_exit, 0);
}

void KillCommand::execute() {
    if (m_argc != 3 || m_argv[1][0] != '-') {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }
    int signum, jobId;
    try {
        signum = std::stoi(m_argv[1]) * -1;
        jobId = std::stoi(m_argv[2]);
    } catch (...) {
        std::cerr << "smash error: kill: invalid arguments" << std::endl;
        return;
    }
    JobsList::JobEntry *job = m_jobsListRef.getJobById(jobId);
    if (!job) {
        std::cerr << "smash error: kill: job-id " << jobId << " does not exist" << std::endl;
        return;
    }
    std::cout << "signal number " << signum << " was sent to pid " << job->m_jobPID << std::endl;
    if (syscall(SYS_kill, job->m_jobPID, signum) == -1) {
        printError("kill");
        return;
    }


}

void AliasCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    if (m_argc == 1) {
        //only alias without args print aliasMap
        for (const auto &p: smash.m_aliasMap)
            std::cout << p.first << "='" << p.second << "'" << std::endl;
        return;
    }
    //check validation of args
    std::string stripped = m_cmdLine.substr(
            m_cmdLine.find("alias") + 5);
    stripped = _trim(stripped);
    size_t equalPos = stripped.find('=');
    if (equalPos == std::string::npos) {
        std::cerr << "smash error: alias: invalid alias format" << std::endl;
        return;
    }
    std::string name = _trim(stripped.substr(0, equalPos));
    std::string value = _trim(stripped.substr(equalPos + 1));
    if (value.size() < 2 || value.front() != '\'' || value.back() != '\'') {
        std::cerr << "smash error: alias: invalid alias format" << std::endl;
        return;
    }
    value = value.substr(1, value.size() - 2);
//    std::regex rx("^\\s*([A-Za-z0-9_]+)='([^']*)'\\s*$");
//    std::smatch m;
//    if (!std::regex_match(stripped, m, rx)) {
//        std::cerr << "smash error: alias: invalid alias format" << std::endl;
//        return;
//    }
//    std::string name = m[1], cmd = m[2];
    static const unordered_set<std::string> reserved = {
            "quit", "jobs", "fg", "cd", "pwd", "showpid", "kill",
            "alias", "unalias", "watchproc", "unsetenv", "chprompt",
            "du", "whoami", "netinfo"
    };
    if (smash.m_aliasMap.count(name) || reserved.count(name)) {
        std::cerr << "smash error: alias: " << name
                  << " already exists or is a reserved command" << std::endl;
        return;
    }
    smash.m_aliasMap[name] = value;

}

void UnAliasCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    if (m_argc == 1) {
        cerr << "smash error: unalias: not enough arguments" << std::endl;
        return;
    }
    if (m_argc >= 2) {
        for (int i = 1; i < m_argc; i++) {
            string name = m_argv[i];
            auto iter = smash.m_aliasMap.find(name);
            if (iter == smash.m_aliasMap.end()) {
                cerr << "smash error: unalias: " << name << " alias does not exist" << std::endl;
                return;
            }
            smash.m_aliasMap.erase(iter);
        }
    }
}

void UnSetEnvCommand::execute() {
    if (m_argc <= 1) {
        std::cerr << "smash error: unsetenv: not enough arguments" << std::endl;
        return;
    }

    for (int i = 1; i < m_argc; ++i) {
        std::string var = m_argv[i];

        if (!envVarExists(var)) {
            std::cerr << "smash error: unsetenv: " << var << " does not exist" << std::endl;
            return;
        }

        if (!removeEnvVar(var)) {
            std::cerr << "smash error: unsetenv failed" << std::endl;
            return;
        }
    }
}

void WatchProcCommand::execute() {
    pid_t pid;
    //==================================Error Handling===================================//
    if (m_argc != 2) {
        std::cerr << "smash error: watchproc: invalid arguments" << std::endl;
        return;
    }
    try {
        pid = std::stoi(m_argv[1]);
    } catch (const std::invalid_argument &error) {
        std::cerr << "smash error: watchproc: invalid arguments" << std::endl;
        return;
    }
    //check if PID doesnt exist
    if (syscall(SYS_kill, pid, 0) == -1 && errno == ESRCH) {
        std::cerr << "smash error: watchproc: pid " << pid << " does not exist " << std::endl;
        return;
    }
    //==================================Parsing Files===================================//
    std::string procPath = "/proc/" + std::to_string(pid);
    std::string procPathStat = procPath + "/stat";
    std::string procPathStatus = procPath + "/status";
//    std::string totalUptime = "/proc/uptime";

    //here for sure the PID exists.
    std::string systemStat1 = readFile("/proc/stat");
//    std::string totalUptime1 = readFile(totalUptime);
    std::string procStat1 = readFile(procPathStat);

    //1sec sleep interval
    struct timespec ts = {0, 1000000000}; //yonadav: create an object insted of &
    syscall(SYS_nanosleep, &ts, NULL);

    std::string systemStat2 = readFile("/proc/stat");
//    std::string totalUptime2 = readFile(totalUptime);
    std::string procStat2 = readFile(procPathStat);
    //also procStatus1 for memory usage
    std::string procStatus1 = readFile(procPathStatus);
    if (procStat1 == "" || procStat2 == "" || procStatus1 == "") {
        return;
    }
    //==================================Parsing Fields===================================//
//    unsigned long utime1 = 0, stime1 = 0, utime2 = 0, stime2 = 0;
    double uptime1 = 0, uptime2 = 0, memoryUsageMB = 0;
//    int field = 0;
//    std::string token;
    std::string line;
//
//    std::istringstream procStatStream1(procStat1);
//    std::istringstream procStatStream2(procStat2);
    std::istringstream statusStream(procStatus1);
//
//    field = 1;
//    while (procStatStream1 >> token) {
//        if (field == 14) utime1 = std::stoul(token);
//        if (field == 15) stime1 = std::stoul(token);
//        if (field > 15) break;
//        field++;
//    }
//    field = 1;
//    while (procStatStream2 >> token) {
//        if (field == 14) utime2 = std::stoul(token);
//        if (field == 15) stime2 = std::stoul(token);
//        if (field > 15) break;
//        field++;
//    }
    unsigned long long utime1 = 0, stime1 = 0, utime2 = 0, stime2 = 0;

    if (!parseUtimeStime(procStat1, utime1, stime1) ||
        !parseUtimeStime(procStat2, utime2, stime2)) {
        std::cerr << "smash error: watchproc: failed to parse /proc data" << std::endl;
        return;
    }

//    uptime1 = std::stod(totalUptime1.substr(0, totalUptime1.find(' ')));
//    uptime2 = std::stod(totalUptime2.substr(0, totalUptime2.find(' ')));

    while (std::getline(statusStream, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream lineStream(line);
            std::string key;
            long valueKB;
            lineStream >> key >> valueKB;
            memoryUsageMB = valueKB / 1024.0;
            break;
        }
    }
    unsigned long long totalTicks1 = 0, totalTicks2 = 0;
    if (!parseTotalCpuTicks(systemStat1, totalTicks1) ||
        !parseTotalCpuTicks(systemStat2, totalTicks2)) {
        std::cerr << "smash error: watchproc: failed to parse /proc/stat" << std::endl;
        return;
    }
    unsigned long long totalTicksDelta = totalTicks2 - totalTicks1;
    //==================================Final Calc===================================//
//    long clockTicksPerSec = sysconf(_SC_CLK_TCK);
//    unsigned long procTicksDelta = (utime2 + stime2) - (utime1 + stime1);
//    double totalTimeDelta = uptime2 - uptime1;
//    double cpuUsagePercent = (static_cast<double>(procTicksDelta) / (totalTimeDelta * clockTicksPerSec)) *
//                             100; //casting to avoid integer division on some systems
    unsigned long long procTicksDelta = (utime2 + stime2) - (utime1 + stime1);

    double cpuUsagePercent = 0.0;
    if (totalTicksDelta != 0) {
        cpuUsagePercent = (static_cast<double>(procTicksDelta) /
                           static_cast<double>(totalTicksDelta)) * 100.0;
    }
    //==================================Printing===================================//
    std::cout << "PID: " << pid
              << " | CPU Usage: " << std::fixed << std::setprecision(1) << cpuUsagePercent << "%"
              << " | Memory Usage: " << std::fixed << std::setprecision(1) << memoryUsageMB << " MB"
              << std::endl;
}

void RedirectionCommand::execute() {
    int flags = O_WRONLY | O_CREAT | (m_override ? O_TRUNC : O_APPEND);
    int mode = 0666; //read+write for everyone
    int fd = syscall(SYS_open, m_outPathPart.c_str(), flags, mode);
    if (fd == -1) {
        printError("open");
        return;
    }
    int stdoutBackup = dup(STDOUT_FILENO);  //backup stdout
    if (stdoutBackup == -1) {
        printError("dup");
        close(fd);
        return;
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        printError("dup2");
        close(fd);
        close(stdoutBackup);
        return;
    }
    close(fd);
    SmallShell &smash = SmallShell::getInstance();
    smash.CreateCommand(m_commandPart.c_str())->execute();
    if (dup2(stdoutBackup, STDOUT_FILENO) == -1) {
        printError("dup2");
    }
    close(stdoutBackup);
}

void PipeCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    int fd[2];
    if (pipe(fd) == -1) {
        printError("pipe");
        return;
    }
    pid_t pid1 = fork();
    if (pid1 < 0) {
        printError("fork");
        return;
    }
    if (pid1 == 0) {        // first child
        setpgrp();
        close(fd[0]); // Close stdin
        if (m_toStderr) {
            dup2(fd[1], STDERR_FILENO);
        } else {
            dup2(fd[1], STDOUT_FILENO);
        }
        close(fd[1]);
        smash.CreateCommand(m_leftCmd.c_str())->execute();
        exit(0);

    }
    pid_t pid2 = fork();
    if (pid2 < 0) {
        printError("fork");
        return;
    }
    if (pid2 == 0) {        // second child
        setpgrp();

        close(fd[1]); // Close stdout
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        smash.CreateCommand(m_rightCmd.c_str())->execute();
        exit(0);
    }
    close(fd[0]);
    close(fd[1]);

    waitpid(pid1, nullptr, 0);
    waitpid(pid2, nullptr, 0);

}

void DiskUsageCommand::execute() {                          //TODO: define no args du, and check logic

    if (m_argc > 2) {
        std::cerr << "smash error: du: too many arguments" << std::endl;
        return;
    }
    std::string path;
    if (m_argc == 2) {
        path = m_argv[1];
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            printError("getcwd");
            return;
        }
        path = std::string(cwd);
    }
    struct stat st;
    if (syscall(SYS_lstat, path.c_str(), &st) == -1) {
        std::cerr << "smash error: du: directory " << path << " does not exist" << std::endl;
        return;
    }
    long totalSizeInKB = recursiveFolderSizeCalc(path, true);
    std::cout << "Total disk usage: " << totalSizeInKB << " KB" << std::endl;
}

void WhoAmICommand::execute() {
    int UID = syscall(SYS_geteuid);
    //now we need to connect the UID w/ the userName which is held in /etc/passwd
    std::string passwdContent = readFile("/etc/passwd");
    if (passwdContent == "") {
        return;
    }
    //parsing by lines, userName:password:UID:GID:GECOS:homeDirectory:shell
    std::istringstream passwdStream(passwdContent);
    std::string line;
    while (std::getline(passwdStream, line)) {
        std::istringstream lineStream(line);
        std::string userName, password, UIDstring, GID, GECOS, homePath, shell;

        std::getline(lineStream, userName, ':');
        std::getline(lineStream, password, ':');
        std::getline(lineStream, UIDstring, ':');
        std::getline(lineStream, GID, ':');
        std::getline(lineStream, GECOS, ':');
        std::getline(lineStream, homePath, ':');
        std::getline(lineStream, shell, ':');

        try {
            if (std::stoi(UIDstring) == UID) {
                std::cout << userName << " " << homePath << std::endl;
                return;
            }
        } catch (...) {
            return;
        }
    }
}

void NetInfo::execute() {
    if (m_argc < 2) {
        std::cerr << "smash error: netinfo: interface not specified" << std::endl;
        return;
    }
    std::string iface = m_argv[1];


    if (if_nametoindex(iface.c_str()) == 0) {
        std::cerr << "smash error: netinfo: interface "
                  << iface << " does not exist" << std::endl;
        return;
    }

    std::string ip, mask;
    if (!getIfaceAddr(iface, ip, mask)) {
        std::cerr << "smash error: netinfo: failed to query interface" << std::endl;
        return;
    }
    std::string gw = getDefaultGateway(iface);
    auto dns = getDnsServers();

    /* ---------- הדפסה ---------- */
    std::cout << "IP Address: " << ip << std::endl;
    std::cout << "Subnet Mask: " << mask << std::endl;
    std::cout << "Default Gateway: " << gw << std::endl;
    std::cout << "DNS Servers: ";
    for (size_t i = 0; i < dns.size(); ++i) {
        std::cout << dns[i];
        if (i + 1 < dns.size()) std::cout << ", ";
    }
    std::cout << std::endl;
}

#pragma endregion

//--------------------EXTERNAL_COMMAND::EXECUTE()--------------------//
void ExternalCommand::execute() {

    pid_t pid = fork();         //maybe need syscall
    if (pid < 0) {
        printError("fork");
        return;
    }
    if (pid == 0) {        // child process
        setpgrp();                                         //new group ID
        if (m_cmdLine.find('*') != std::string::npos ||
            m_cmdLine.find('?') != std::string::npos) {   // complex external command
            char *const args[] = {
                    const_cast<char *>("/bin/bash"),
                    const_cast<char *>("-c"),
                    const_cast<char *>(m_cmdLine.c_str()),
                    nullptr
            };
            execv("/bin/bash", args);
        } else {                                            // simple external command
            std::vector<char *> argv_cstr;
            for (const auto &arg: m_argv) {
                argv_cstr.push_back(const_cast<char *>(arg.c_str()));
            }
            argv_cstr.push_back(nullptr);
            execvp(argv_cstr[0], argv_cstr.data());
        }

        printError("exec");                      // exec dont return so if we got here its an error
        syscall(SYS_exit, 1);
    }
    SmallShell &smash = SmallShell::getInstance();
    if (m_isBackgroundCommand) {
        Command *command = smash.CreateCommand(getCmdLineFull().c_str());
        smash.getJobsList().addJob(command, false, pid);
        delete command;
    } else {
        smash.setFgProcPID(pid);
        smash.setFgProcCmd(m_cmdLine);
        if (syscall(SYS_wait4, pid, nullptr, 0, nullptr) == -1) printError("waitpid");
        SmallShell::getInstance().clearFgJob();
    }
}
//--------------------SMASH CLASS--------------------//
#pragma region SMASH CLASS

SmallShell::SmallShell() {
}

SmallShell::~SmallShell() {
}

Command *SmallShell::CreateCommand(const char *cmd_line) {
    std::string cmd_s = _trim(string(cmd_line));
    if (isAlias(cmd_s)) {
        cmd_s = fixAliasCmdLine(cmd_s);
    }
    std::string firstWord = cmd_s.substr(0, cmd_s.find_first_of(" \n"));
    firstWord = removeBackgroundSign(firstWord); //cuz we can have "kill&" != "kill"

    //-------------------------------------------------------------------------------------//
    if (firstWord == "alias") return new AliasCommand(cmd_s);
    //-------------------------------------------------------------------------------------//
    if (cmd_s.find('>') != std::string::npos) return new RedirectionCommand(cmd_s);
    if (cmd_s.find('|') != std::string::npos) return new PipeCommand(cmd_s);
    if (firstWord == "unalias") return new UnAliasCommand(cmd_s);
    if (firstWord == "du") return new DiskUsageCommand(cmd_s);
    if (firstWord == "whoami") return new WhoAmICommand(cmd_s);
    if (firstWord == "netinfo") return new NetInfo(cmd_s);
    //-------------------------------------------------------------------------------------//
    if (firstWord == "chprompt") return new ChPromptCommand(cmd_s);
    if (firstWord == "showpid") return new ShowPidCommand(cmd_s);
    if (firstWord == "pwd") return new GetCurrDirCommand(cmd_s);
    if (firstWord == "cd") return new ChangeDirCommand(cmd_s, this->getLastPWD());
    if (firstWord == "jobs") return new JobsCommand(cmd_s, this->getJobsList());
    if (firstWord == "fg") return new ForegroundCommand(cmd_s, this->getJobsList());
    if (firstWord == "quit") return new QuitCommand(cmd_s, this->getJobsList());
    if (firstWord == "kill") return new KillCommand(cmd_s, this->getJobsList());
    if (firstWord == "unsetenv") return new UnSetEnvCommand(cmd_s);
    if (firstWord == "watchproc") return new WatchProcCommand(cmd_s);
    //-------------------------------------------------------------------------------------//
    return new ExternalCommand(cmd_s);
}

void SmallShell::executeCommand(const char *cmd_line) {
    Command *cmd = CreateCommand(cmd_line);
    cmd->execute();
    delete cmd;
    //Please note that you must fork smash process for some commands (e.g., external commands....)
}

//GET-SET
std::string SmallShell::getPrompt() {
    return this->m_prompt;
}

void SmallShell::setPrompt(std::string value) {
    this->m_prompt = value;
}

std::string SmallShell::getLastPWD() {
    return this->m_lastPWD;
}

void SmallShell::setLastPWD(std::string value) {
    this->m_lastPWD = value;
}

pid_t SmallShell::getFgProcPID() const {
    return this->m_fgProcPID;
}

void SmallShell::setFgProcPID(pid_t pid) {
    this->m_fgProcPID = pid;
}

std::string SmallShell::getFgProcCmd() const {
    return m_fgCmd;
}

void SmallShell::setFgProcCmd(std::string cmdLine) {
    m_fgCmd = cmdLine;
}

void SmallShell::clearFgJob() {
    m_fgProcPID = -1;
    m_fgCmd = "";
}

JobsList &SmallShell::getJobsList() {
    return m_jobsList;
}

//ALIAS HANDLING
bool SmallShell::isAlias(std::string cmd_line) {
    std::string firstWord = cmd_line.substr(0, cmd_line.find_first_of(" \n"));
    firstWord = removeBackgroundSign(firstWord); //cuz we can have "kill&" != "kill"
    auto iter = m_aliasMap.find(firstWord);
    if (iter != m_aliasMap.end()) {
        return true;
    }
    return false;
}

std::string SmallShell::fixAliasCmdLine(std::string cmd_line) {
    string cmd_s;
    std::string firstWord = cmd_line.substr(0, cmd_line.find_first_of(" \n"));
    firstWord = removeBackgroundSign(firstWord);
    std::string rest = cmd_line.substr(cmd_line.find_first_of(" \n") + 1, cmd_line.length());
    firstWord = m_aliasMap.find(firstWord)->second;
    return firstWord + rest;
    //TODO: check & rules
}

#pragma endregion

//--------------------JOBSLIST CLASS--------------------//
#pragma region JOBSLIST CLASS

int JobsList::calcNewID() {
    if (m_jobs.size() == 0) return 1;
    auto last = --m_jobs.end();
    return last->first + 1;
}

void JobsList::removeFinishedJobs() {
    auto iter = m_jobs.begin();
    while (iter != m_jobs.end()) {
        int status; //might need to use in the future, currently unsure if status is needed
        long result = syscall(SYS_wait4, iter->second.m_jobPID, &status, WNOHANG, NULL);

        if (result == -1) {
            if (errno == ECHILD) {
                iter = m_jobs.erase(iter);
                continue;
            }
            printError("waitpid");
        }
        if (result <= 0) ++iter;
        else iter = m_jobs.erase(iter); //TODO: make sure i dont want to remove failed jobs where waitpid() returned -1
    }
}

void JobsList::addJob(Command *cmd, bool isStopped, pid_t jobPID) {
    this->removeFinishedJobs();
    int uniqueID = calcNewID();
    std::string cmdLine = cmd->getCmdLineFull();
    JobEntry newJob(cmdLine, jobPID, uniqueID, isStopped);
    m_jobs.insert({uniqueID, newJob});
}

void JobsList::printJobsList() {
    this->removeFinishedJobs();
    auto iter = m_jobs.begin();
    while (iter != m_jobs.end()) {
        std::cout << "[" << iter->first << "] " << iter->second.m_jobCommandString << std::endl;
        ++iter;
    }
}

void JobsList::killAllJobs() {
    this->removeFinishedJobs();
    std::cout << "smash: sending SIGKILL signal to " << m_jobs.size() << " jobs:" << std::endl;
    auto iter = m_jobs.begin();
    while (iter != m_jobs.end()) {
        std::cout << iter->second.m_jobPID << ": " << iter->second.m_jobCommandString << std::endl;
        int result = syscall(SYS_kill, iter->second.m_jobPID, SIGKILL);
        if (result == -1) printError("kill");
        ++iter; //jobs will be removed anyway on next call for any method of JobsList
    }
    this->removeFinishedJobs();// not sure if needed
}

JobsList::JobEntry *JobsList::getJobById(int jobId) {
    this->removeFinishedJobs();
    auto iter = m_jobs.find(jobId);
    if (iter == m_jobs.end()) return nullptr;

    return &iter->second;
}

void JobsList::removeJobById(int jobId) {
    this->removeFinishedJobs();
    auto iter = m_jobs.find(jobId);
    if (iter == m_jobs.end()) return;

    m_jobs.erase(iter);
}

JobsList::JobEntry *JobsList::getLastJob(int *lastJobId) {
    this->removeFinishedJobs();
    if (m_jobs.size() == 0) return nullptr;
    auto last = --m_jobs.end();
    *lastJobId = last->second.m_jobID;
    return &last->second;
}

JobsList::JobEntry *JobsList::getLastStoppedJob(int *jobId) {
    this->removeFinishedJobs();
    if (m_jobs.size() == 0) return nullptr;
    auto iterReversed = --m_jobs.end();
    while (iterReversed->second.m_isStopped != true && iterReversed != m_jobs.begin()) {
        --iterReversed;
    }
    if (iterReversed == m_jobs.begin() && iterReversed->second.m_isStopped != true) {
        return nullptr;
    }
    *jobId = iterReversed->second.m_jobID;
    return &iterReversed->second;
}

#pragma endregion

//--------------------COMMAND CLASS--------------------//
#pragma region COMMAND CLASS

Command::Command(const std::string cmd_line) {
    std::string cleanLine = _trim(removeBackgroundSign(cmd_line));
    this->m_cmdLine = cleanLine;
    this->m_argc = parseCommandLine(cleanLine, &this->m_argv);
    this->m_isBackgroundCommand = isBackgroundCommand(cmd_line);
}

Command::~Command() = default;

RedirectionCommand::RedirectionCommand(const std::string cmd_line) : Command(cmd_line) {
    size_t sep = m_cmdLine.find('>');
    // if (sep == std::string::npos) {
    //     throw std::runtime_error("internal: no redirection symbol");
    // }

    // append (>>)
    if (sep + 1 < m_cmdLine.size() && m_cmdLine[sep + 1] == '>') {
        m_override = false;  // >>
        ++sep;
    } else {
        m_override = true;    // >
    }

    m_commandPart = _trim(m_cmdLine.substr(0, m_cmdLine.find('>')));

    size_t filePos = m_cmdLine.find_first_not_of(" \t\r\n", sep + 1);
    // if (filePos == std::string::npos) {
    //     throw std::runtime_error("smash error: redirection – missing file name");
    // }
    m_outPathPart = _trim(m_cmdLine.substr(filePos));
}

PipeCommand::PipeCommand(const std::string cmd_line) : Command(cmd_line) {
    size_t pos = cmd_line.find("|&");
    if (pos != std::string::npos) {
        m_toStderr = true;
    } else {
        pos = cmd_line.find('|');
        m_toStderr = false;
    }

    m_leftCmd = _trim(cmd_line.substr(0, pos));
    m_rightCmd = _trim(cmd_line.substr(pos + (m_toStderr ? 2 : 1)));
}

bool Command::getIsBackgroundCommand() {
    return this->m_isBackgroundCommand;
}

std::string Command::getCmdLine() {
    return this->m_cmdLine;
}

std::string Command::getCmdLineFull() {
    if (this->m_isBackgroundCommand) {
        return this->m_cmdLine + "&";
    }
    return this->m_cmdLine;
}

#pragma endregion
