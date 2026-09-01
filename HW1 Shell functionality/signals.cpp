#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;

void ctrlCHandler(int sig_num) {
    std::cout << "smash: got ctrl-C" << endl;
    SmallShell &smash = SmallShell::getInstance();
    if (smash.getFgProcPID() > 0) {
        if (kill(smash.getFgProcPID(), SIGINT) == -1) {
            perror("smash error: kill failed");
        } else {
            std::cout << "smash: process " << smash.getFgProcPID() << " was killed" << std::endl;
            smash.clearFgJob();
        }
    }
}
