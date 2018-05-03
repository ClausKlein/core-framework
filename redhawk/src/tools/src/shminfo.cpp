/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file 
 * distributed with this source distribution.
 * 
 * This file is part of REDHAWK core.
 * 
 * REDHAWK core is free software: you can redistribute it and/or modify it 
 * under the terms of the GNU Lesser General Public License as published by the 
 * Free Software Foundation, either version 3 of the License, or (at your 
 * option) any later version.
 * 
 * REDHAWK core is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License 
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <signal.h>

#include <ossie/shm/System.h>

#include "ShmVisitor.h"

using redhawk::shm::SuperblockFile;

namespace {
    static std::string executable;

    static void usage()
    {
        std::cout << "Usage: " << executable << " [OPTION]..." << std::endl;
        std::cout << "Display status of shared memory file system and REDHAWK heaps." << std::endl;
        std::cout << std::endl;
        std::cout << "  -h, --help           display this help and exit" << std::endl;
        std::cout << "  -a, --all            include non-REDHAWK heap shared memory files" << std::endl;
        std::cout << "      --format=FORMAT  display sizes in FORMAT (default 'auto')" << std::endl;
        std::cout << "      --version        output version information and exit" << std::endl;
        std::cout << std::endl;
        std::cout << "FORMAT may be one of the following:" << std::endl;
        std::cout << "  auto        use human readable sizes (e.g., 4K, 2.4G)" << std::endl;
        std::cout << "  b           bytes" << std::endl;
        std::cout << "  k           kilobytes" << std::endl;
    }
}

class SizeFormatter
{
public:
    enum Format {
        BYTES,
        KILOBYTES,
        AUTO
    };

    explicit SizeFormatter(Format format=AUTO) :
        _format(format)
    {
    }

    static Format Parse(const std::string& strval)
    {
        std::string format_str;
        std::transform(strval.begin(), strval.end(), std::back_inserter(format_str), ::tolower);
        if (format_str == "auto") {
            return AUTO;
        } else if (format_str == "k") {
            return KILOBYTES;
        } else if (format_str == "b") {
            return BYTES;
        } else {
            throw std::invalid_argument("invalid format '" + strval + "'");
        }
    }

    void setFormat(Format format)
    {
        _format = format;
    }

    void setFormat(const std::string& strval)
    {
        setFormat(SizeFormatter::Parse(strval));
    }

    std::string operator() (size_t size)
    {
        std::ostringstream oss;
        switch (_format) {
        case KILOBYTES:
            oss << (size/1024) << "K";
            break;
        case AUTO:
            _toHumanReadable(oss, size);
            break;
        case BYTES:
        default:
            oss << size;
            break;
        }
        return oss.str();
    }

private:
    void _toHumanReadable(std::ostream& oss, size_t size)
    {
        const char* suffix[] = { "", "K", "M", "G", "T", 0 };
        double val = size;
        int pow = 0;
        while ((val >= 1024.0) && (suffix[pow+1])) {
            val /= 1024.0;
            pow++;
        }
        if (pow == 0) {
            oss << val;
        } else {
            oss << std::fixed << std::setprecision(1) << val << suffix[pow];
        }
    }

    Format _format;
};

class Info : public ShmVisitor
{
public:
    Info() :
        _all(false),
        _format()
    {
    }

    void setDisplayAll(bool all)
    {
        _all = all;
    }

    void setSizeFormatter(SizeFormatter& format)
    {
        _format = format;
    }

    virtual void visitHeap(SuperblockFile& heap)
    {
        pid_t creator = heap.creator();
        std::cout << "  creator:     " << creator;
        if (kill(creator, 0) != 0) {
            std::cout << " (defunct)";
        }
        std::cout << std::endl;
        std::cout << "  refcount:    " << heap.refcount() << std::endl;

        SuperblockFile::Statistics stats = heap.getStatistics();
        std::cout << "  total size:  " << _format(stats.size) << std::endl;
        std::cout << "  total used:  " << _format(stats.used) << std::endl;
        std::cout << "  superblocks: " << stats.superblocks << std::endl;
        std::cout << "  unused:      " << stats.unused << std::endl;
    }

    virtual void visitFile(const std::string& name)
    {
        if (isHeap(name)) {
            displayFile(name, "REDHAWK heap");
        } else if (_all) {
            displayFile(name, "other");
        }
    }

    virtual void heapException(const std::string& name, const std::exception& exc)
    {
        std::cerr << "error: " << exc.what() << std::endl;
    }

protected:
    void displayFile(const std::string& name, const std::string& type)
    {
        std::string path = getShmPath() + "/" + name;
        std::cout << std::endl << path << std::endl;;

        struct stat status;
        if (stat(path.c_str(), &status)) {
            std::cout << "  (cannot access)" << std::endl;
            return;
        }
        std::cout << "  file size:   " << _format(status.st_size) << std::endl;
        std::cout << "  allocated:   " << _format(status.st_blocks * 512) << std::endl;
        std::cout << "  type:        " << type << std::endl;
    }

    bool _all;
    SizeFormatter _format;
};

int main(int argc, char* argv[])
{
    // Save the executable name for output, removing any paths
    executable = argv[0];
    std::string::size_type pos = executable.rfind('/');
    if (pos != std::string::npos) {
        executable.erase(0, pos + 1);
    }

    struct option long_options[] = {
        { "format", required_argument, 0, 'f' },
        { "help", no_argument, 0, 'h' },
        { "all", no_argument, 0, 'a' },
        { "version", no_argument, 0, 'V' },
        { 0, 0, 0, 0 }
    };

    Info info;
    SizeFormatter format;

    int opt;
    while ((opt = getopt_long(argc, argv, "ah", long_options, NULL)) != -1) {
        switch (opt) {
        case 'a':
            info.setDisplayAll(true);
            break;
        case 'f':
            try {
                format.setFormat(optarg);
            } catch (const std::exception& exc) {
                std::cerr << exc.what() << std::endl;
                return -1;
            }
            break;
        case 'h':
            usage();
            return 0;
        case 'V':
            std::cout << executable << " version " << VERSION << std::endl;
            return 0;
        default:
            std::cerr << "Try `" << executable << " --help` for more information." << std::endl;
            return -1;
        }
    };

    std::cout << redhawk::shm::getSystemPath() << std::endl;
    std::cout << "  size: " << format(redhawk::shm::getSystemTotalMemory()) << std::endl;
    std::cout << "  free: " << format(redhawk::shm::getSystemFreeMemory()) << std::endl;

    info.setSizeFormatter(format);
    try {
        info.visit();
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << std::endl;
        return -1;
    } 
    return 0;
}
