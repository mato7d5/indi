/*******************************************************************************
  Copyright(c) 2018 Jasem Mutlaq. All rights reserved.

  Arduino ST4 Driver.

  For this project: https://github.com/kevinferrare/arduino-st4

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
*******************************************************************************/

#include "arduino_st4.h"

#include "indicom.h"
#include "connectionplugins/connectionserial.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <termios.h>
#include <sys/ioctl.h>

// We declare an auto pointer to ArduinoST4.
std::unique_ptr<ArduinoST4> arduinoST4(new ArduinoST4());

#define FLAT_TIMEOUT 3

ArduinoST4::ArduinoST4() : GI(this)
{
    setVersion(1, 0);
}

bool ArduinoST4::initProperties()
{
    INDI::DefaultDevice::initProperties();

    GI::initProperties(MOTION_TAB);

    setDriverInterface(AUX_INTERFACE | GUIDER_INTERFACE);

    addAuxControls();

    serialConnection = new Connection::Serial(this);
    serialConnection->registerHandshake([&]()
    {
        return Handshake();
    });
    serialConnection->setDefaultBaudRate(Connection::Serial::B_57600);
    // Arduino default port
    serialConnection->setDefaultPort("/dev/ttyACM0");
    registerConnection(serialConnection);

    return true;
}

bool ArduinoST4::updateProperties()
{
    INDI::DefaultDevice::updateProperties();
    GI::updateProperties();

    return true;
}

const char *ArduinoST4::getDefaultName()
{
    return static_cast<const char *>("Arduino ST4");
}

bool ArduinoST4::Handshake()
{
    if (isSimulation())
    {
        LOGF_INFO("Connected successfully to simulated %s.", getDeviceName());
        return true;
    }

    PortFD = serialConnection->getPortFD();

    return true;
}

bool ArduinoST4::Disconnect()
{
    sendCommand("DISCONNECT#");

    return INDI::DefaultDevice::Disconnect();
}

bool ArduinoST4::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    // Check guider interface
    if (GI::processNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

IPState ArduinoST4::GuideNorth(uint32_t ms)
{
    LOGF_DEBUG("Guiding: N %.0f ms", ms);

    if (GuideNSTID)
    {
        IERmTimer(GuideNSTID);
        GuideNSTID = 0;
    }

    if (sendCommand("DEC+#") == false)
        return IPS_ALERT;

    guideDirection = ARD_N;
    GuideNSTID      = IEAddTimer(static_cast<int>(ms), guideTimeoutHelperN, this);
    return IPS_BUSY;
}

IPState ArduinoST4::GuideSouth(uint32_t ms)
{
    LOGF_DEBUG("Guiding: S %.0f ms", ms);

    if (GuideNSTID)
    {
        IERmTimer(GuideNSTID);
        GuideNSTID = 0;
    }

    if (sendCommand("DEC-#") == false)
        return IPS_ALERT;

    guideDirection = ARD_S;
    GuideNSTID      = IEAddTimer(static_cast<int>(ms), guideTimeoutHelperS, this);
    return IPS_BUSY;
}

IPState ArduinoST4::GuideEast(uint32_t ms)
{
    LOGF_DEBUG("Guiding: E %.0f ms", ms);

    if (GuideWETID)
    {
        IERmTimer(GuideWETID);
        GuideWETID = 0;
    }

    if (sendCommand("RA+#") == false)
        return IPS_ALERT;

    guideDirection = ARD_E;
    GuideWETID      = IEAddTimer(static_cast<int>(ms), guideTimeoutHelperE, this);
    return IPS_BUSY;
}

IPState ArduinoST4::GuideWest(uint32_t ms)
{
    LOGF_DEBUG("Guiding: W %.0f ms", ms);

    if (GuideWETID)
    {
        IERmTimer(GuideWETID);
        GuideWETID = 0;
    }

    if (sendCommand("RA-#") == false)
        return IPS_ALERT;

    guideDirection = ARD_W;
    GuideWETID      = IEAddTimer(static_cast<int>(ms), guideTimeoutHelperE, this);
    return IPS_BUSY;
}

//GUIDE The timer helper functions.
void ArduinoST4::guideTimeoutHelperN(void *p)
{
    static_cast<ArduinoST4 *>(p)->guideTimeout(ARD_N);
}
void ArduinoST4::guideTimeoutHelperS(void *p)
{
    static_cast<ArduinoST4 *>(p)->guideTimeout(ARD_S);
}
void ArduinoST4::guideTimeoutHelperW(void *p)
{
    static_cast<ArduinoST4 *>(p)->guideTimeout(ARD_W);
}
void ArduinoST4::guideTimeoutHelperE(void *p)
{
    static_cast<ArduinoST4 *>(p)->guideTimeout(ARD_E);
}

void ArduinoST4::guideTimeout(ARDUINO_DIRECTION direction)
{
    if (direction == ARD_N || direction == ARD_S)
    {
        if (sendCommand("DEC0#"))
        {
            GuideNSNP.setState(IPS_IDLE);
            LOG_DEBUG("Guiding: DEC axis stopped.");
        }
        else
        {
            GuideNSNP.setState(IPS_ALERT);
            LOG_ERROR("Failed to stop DEC axis.");
        }

        GuideNSTID            = 0;
        GuideNSNP[0].setValue(0);
        GuideNSNP[1].setValue(0);
        GuideNSNP.apply();
    }

    if (direction == ARD_W || direction == ARD_E)
    {
        if (sendCommand("RA0#"))
        {
            GuideNSNP.setState(IPS_IDLE);
            LOG_DEBUG("Guiding: RA axis stopped.");
        }
        else
        {
            LOG_ERROR("Failed to stop RA axis.");
            GuideNSNP.setState(IPS_ALERT);
        }

        GuideWENP[0].setValue(0);
        GuideWENP[1].setValue(0);
        GuideWETID            = 0;
        GuideWENP.apply();
    }
}

bool ArduinoST4::sendCommand(const char *cmd)
{
    int nbytes_read = 0, nbytes_written = 0, tty_rc = 0;
    char res[8] = {0};
    LOGF_DEBUG("CMD <%s>", cmd);

    if (!isSimulation())
    {
        tcflush(PortFD, TCIOFLUSH);
        if ( (tty_rc = tty_write_string(PortFD, cmd, &nbytes_written)) != TTY_OK)
        {
            char errorMessage[MAXRBUF];
            tty_error_msg(tty_rc, errorMessage, MAXRBUF);
            LOGF_ERROR("Serial write error: %s", errorMessage);
            return false;
        }
    }

    if (isSimulation())
    {
        strncpy(res, "OK#", 8);
        nbytes_read = 3;
    }
    else
    {
        if ( (tty_rc = tty_read_section(PortFD, res, '#', ARDUINO_TIMEOUT, &nbytes_read)) != TTY_OK)
        {
            char errorMessage[MAXRBUF];
            tty_error_msg(tty_rc, errorMessage, MAXRBUF);
            LOGF_ERROR("Serial read error: %s", errorMessage);
            return false;
        }
    }

    res[nbytes_read - 1] = '\0';
    LOGF_DEBUG("RES <%s>", res);

    return true;
}
