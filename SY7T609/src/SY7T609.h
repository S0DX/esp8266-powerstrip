#ifndef SY7T609_H
#define SY7T609_H

#include <Arduino.h>
#include <SPI.h>
#include <HardwareSerial.h>

#ifdef ESP8266
#define SY7T609_DEFAULT_CS_PIN 15
#define SY7T609_SPI_CLOCK 4000000
#else
#define SY7T609_DEFAULT_CS_PIN 10
#define SY7T609_SPI_CLOCK 1000000
#endif

#define SY7T609_SPI_MODE SPI_MODE3

class SY7T609 {
public:
    enum InterfaceType {
        INTERFACE_SPI,
        INTERFACE_UART
    };

    enum AlarmType {
        ALARM_UNDERTEMP = 0,
        ALARM_OVERTEMP = 1,
        ALARM_UNDERVOLT = 2,
        ALARM_OVERVOLT = 3,
        ALARM_OVERCURRENT = 4,
        ALARM_OVERPOWER = 5,
        ALARM_UNDERFREQ = 6,
        ALARM_OVERFREQ = 7,
        ALARM_VDROPOUT = 8,
        ALARM_VSAG = 9,
        ALARM_VSURGE = 10
    };

    enum CalibrationCommand {
        CAL_V_GAIN = 0x000010,
        CAL_I_GAIN = 0x000020,
        CAL_V_OFFSET = 0x000040,
        CAL_I_OFFSET = 0x000080,
        CAL_TEMP = 0x000100,
        CAL_I_GAIN_PWR = 0x010000
    };

    enum Command {
        CMD_NOP = 0x000000,
        CMD_SAVE = 0x000001,
        CMD_AUTO_REPORT = 0x000002,
        CMD_STOP_REPORT = 0x000003,
        CMD_CLEAR_ENERGY = 0x000004,
        CMD_RESET = 0x000008
    };

    enum DIOPin {
        DIO_1 = 1,
        DIO_5 = 5,
        DIO_7 = 7,
        DIO_8 = 8
    };

    struct MeasurementData {
        uint32_t vrms;
        uint32_t irms;
        int32_t power;
        int32_t var;
        int32_t va;
        int32_t pf;
        uint32_t frequency;
        uint32_t temperature;
        int32_t vavg;
        int32_t iavg;
        int32_t avgpower;
    };

    struct FundamentalData {
        uint32_t vfund;
        uint32_t ifund;
        int32_t pfund;
        int32_t qfund;
        int32_t vafund;
    };

    struct HarmonicData {
        uint32_t vharm;
        uint32_t iharm;
        int32_t pharm;
        int32_t qharm;
        int32_t vaharm;
    };

    struct EnergyData {
        uint32_t eppcnt;
        uint32_t epmcnt;
        uint32_t epncnt;
        uint32_t eqncnt;
        uint32_t esncnt;
    };

    struct MinMaxData {
        uint32_t lo;
        uint32_t hi;
    };

    struct PeakData {
        uint32_t vpeak;
        uint32_t ipeak;
    };

    SY7T609();
    ~SY7T609();

    bool begin(SPISettings settings, uint8_t csPin);
    bool begin(uint32_t baud);
    
    bool begin(SPISettings settings, uint8_t csPin, uint8_t maxRetries);
    bool begin(uint32_t baud, uint8_t maxRetries);

    bool readRegister(uint8_t addr, uint32_t& value);
    bool writeRegister(uint8_t addr, uint32_t value);
    
    bool readRegister(uint8_t addr, uint32_t& value, uint8_t retries);
    bool writeRegister(uint8_t addr, uint32_t value, uint8_t retries);
    
    bool readRegisterValidated(uint8_t addr, uint32_t& value, uint8_t retries);
    
    bool verifyConnection();
    uint32_t getLastError();
    void clearErrorCounter();
    uint32_t getErrorCount();

    uint32_t readVRMS();
    uint32_t readIRMS();
    int32_t readPower();
    int32_t readVAR();
    int32_t readVA();
    int32_t readPF();
    uint32_t readFrequency();
    uint32_t readTemperature();
    int32_t readVAVG();
    int32_t readIAVG();
    uint32_t readDivisor();
    uint64_t readFrame();
    int32_t readAvgPower();

    MeasurementData readMeasurement();
    bool readMeasurementFast(MeasurementData& data);
    FundamentalData readFundamental();
    HarmonicData readHarmonic();

    bool readEnergyCounters(EnergyData& data);
    bool clearEnergyCounters();
    bool setBucketSize(uint32_t bucketH, uint32_t bucketL);

    bool readCurrentMinMax(MinMaxData& data);
    bool readVoltageMinMax(MinMaxData& data);
    bool readPeaks(PeakData& data);
    bool resetMinMax();

    uint32_t readAlarms();
    bool setAlarmMask(DIOPin pin, uint32_t mask);
    uint32_t getAlarmCount(AlarmType type);
    bool clearAlarm(AlarmType type);

    bool setVoltageSagThreshold(uint32_t value);
    bool setVoltageSurgeThreshold(uint32_t value);
    bool setUnderVoltageThreshold(uint32_t value);
    bool setOverVoltageThreshold(uint32_t value);
    bool setVoltageDropoutThreshold(uint32_t value);
    bool setOverCurrentThreshold(uint32_t value);
    bool setOverPowerThreshold(uint32_t value);
    bool setUnderTempThreshold(int32_t value);
    bool setOverTempThreshold(int32_t value);
    bool setUnderFreqThreshold(uint32_t value);
    bool setOverFreqThreshold(uint32_t value);

    bool setVoltageHoldTime(uint32_t cycles);
    bool setCurrentHoldTime(uint32_t cycles);
    bool setPowerHoldTime(uint32_t cycles);
    bool setTemperatureHoldTime(uint32_t cycles);
    bool setFrequencyHoldTime(uint32_t cycles);

    bool calibrateVoltageGain(uint32_t vrmstarget);
    bool calibrateCurrentGain(uint32_t irmstarget);
    bool calibrateCurrentGainPower(uint32_t powertarget);
    bool calibrateVoltageOffset();
    bool calibrateCurrentOffset();
    bool setCalibrationTargets(uint32_t vavgtarget, uint32_t iavgtarget, uint32_t vrmstarget, uint32_t irmstarget, uint32_t powertarget);

    bool configureDIO(DIOPin pin, uint8_t direction);
    bool setDIOPolarity(DIOPin pin, uint8_t polarity);
    uint32_t readDIO();
    bool writeDIO(DIOPin pin, uint8_t value);
    bool setDIO(DIOPin pin);
    bool clearDIO(DIOPin pin);

    bool indirectRead(uint16_t addr, uint32_t& value);
    bool indirectWrite(uint16_t addr, uint32_t value);

    bool saveToFlash();
    bool loadFromFlash();
    bool clearFlash(uint8_t storage);

    bool softReset();
    bool setAccumulation(uint16_t accumCycles, uint32_t accum);
    bool setHarm(uint8_t harmonic);

    bool setVSCALE(uint32_t value);
    bool setISCALE(uint32_t value);
    bool setPSCALE(uint32_t value);
    bool setPFSCALE(uint32_t value);
    bool setFSCALE(uint32_t value);
    bool setTSCALE(uint32_t value);
    bool setPhaseComp(int32_t value);
    bool setHPF(uint8_t voltageHPF, uint8_t currentHPF);
    bool setAutoHPF(bool voltage, bool current);
    bool setSwapVoltage(bool swap);
    bool setSwapCurrent(bool swap);
    bool setIShift(bool enable);
    bool setPShift(bool enable);

    uint32_t getFWVersion();
    uint32_t readControl();
    bool writeControl(uint32_t value);

private:
    InterfaceType _interfaceType;
    uint8_t _csPin;
    uint32_t _baud;
    SPISettings _spiSettings;
    HardwareSerial* _serial;
    uint8_t _maxRetries;
    uint32_t _errorCount;
    uint32_t _lastError;

    bool isSPI() const { return _interfaceType == INTERFACE_SPI; }
    bool isUART() const { return _interfaceType == INTERFACE_UART; }

    uint32_t spiTransfer(uint8_t addr, uint32_t data, bool read);
    bool uartCommand(uint32_t cmd, uint32_t& response);
    bool uartCommand(uint32_t cmd);
    bool writeCommand(Command cmd);
    bool writeCalibrationCommand(CalibrationCommand cmd);
    bool waitForCalibrationComplete(uint32_t timeout);

    static const uint8_t REG_COMMAND = 0x00;
    static const uint8_t REG_FW_VERSION = 0x01;
    static const uint8_t REG_CONTROL = 0x02;
    static const uint8_t REG_DIVISOR = 0x04;
    static const uint8_t REG_FRAME = 0x05;
    static const uint8_t REG_ALARMS = 0x07;
    static const uint8_t REG_DIO_STATE = 0x08;
    static const uint8_t REG_CTEMP = 0x0D;
    static const uint8_t REG_VAVG = 0x0F;
    static const uint8_t REG_IAVG = 0x10;
    static const uint8_t REG_VRMS = 0x11;
    static const uint8_t REG_IRMS = 0x12;
    static const uint8_t REG_POWER = 0x13;
    static const uint8_t REG_VAR = 0x14;
    static const uint8_t REG_VA = 0x15;
    static const uint8_t REG_PF = 0x16;
    static const uint8_t REG_FREQUENCY = 0x17;
    static const uint8_t REG_AVGPOWER = 0x18;
    static const uint8_t REG_VFUND = 0x19;
    static const uint8_t REG_IFUND = 0x1A;
    static const uint8_t REG_PFUND = 0x1B;
    static const uint8_t REG_QFUND = 0x1C;
    static const uint8_t REG_VAFUND = 0x1D;
    static const uint8_t REG_VHARM = 0x1E;
    static const uint8_t REG_IHARM = 0x1F;
    static const uint8_t REG_PHARM = 0x20;
    static const uint8_t REG_QHARM = 0x21;
    static const uint8_t REG_VAHARM = 0x22;
    static const uint8_t REG_EPPCNT = 0x23;
    static const uint8_t REG_EPMCNT = 0x24;
    static const uint8_t REG_EPNCNT = 0x25;
    static const uint8_t REG_EQNCNT = 0x28;
    static const uint8_t REG_ESNCNT = 0x2B;
    static const uint8_t REG_ILO = 0x2C;
    static const uint8_t REG_IHI = 0x2D;
    static const uint8_t REG_IPEAK = 0x2E;
    static const uint8_t REG_VLO = 0x2F;
    static const uint8_t REG_VHI = 0x30;
    static const uint8_t REG_VPEAK = 0x31;
    static const uint8_t REG_DEVADDR = 0x32;
    static const uint8_t REG_DIO_DIR = 0x33;
    static const uint8_t REG_DIO_POL = 0x34;
    static const uint8_t REG_DIO_SET = 0x35;
    static const uint8_t REG_DIO_RST = 0x36;
    static const uint8_t REG_MASK1 = 0x37;
    static const uint8_t REG_MASK5 = 0x38;
    static const uint8_t REG_MASK7 = 0x39;
    static const uint8_t REG_MASK8 = 0x3A;
    static const uint8_t REG_ALARM_STICKY = 0x3D;
    static const uint8_t REG_ALARM_SET = 0x3E;
    static const uint8_t REG_ALARM_RESET = 0x3F;
    static const uint8_t REG_BUCKETL = 0x40;
    static const uint8_t REG_BUCKETH = 0x41;
    static const uint8_t REG_PHASECOMP = 0x42;
    static const uint8_t REG_IROFF = 0x43;
    static const uint8_t REG_VROFF = 0x44;
    static const uint8_t REG_POFF = 0x45;
    static const uint8_t REG_IGAIN = 0x47;
    static const uint8_t REG_VGAIN = 0x48;
    static const uint8_t REG_IOFFS = 0x49;
    static const uint8_t REG_VOFFS = 0x4A;
    static const uint8_t REG_TGAIN = 0x4B;
    static const uint8_t REG_TOFFS = 0x4C;
    static const uint8_t REG_ISCALE = 0x4F;
    static const uint8_t REG_VSCALE = 0x50;
    static const uint8_t REG_PSCALE = 0x51;
    static const uint8_t REG_PFSCALE = 0x52;
    static const uint8_t REG_FSCALE = 0x53;
    static const uint8_t REG_TSCALE = 0x54;
    static const uint8_t REG_ACCUMCYC = 0x56;
    static const uint8_t REG_ACCUM = 0x57;
    static const uint8_t REG_CALCYC = 0x58;
    static const uint8_t REG_CALITR = 0x59;
    static const uint8_t REG_HARM = 0x5A;
    static const uint8_t REG_IAVGTARGET = 0x5B;
    static const uint8_t REG_VAVGTARGET = 0x5C;
    static const uint8_t REG_IRMSTARGET = 0x5D;
    static const uint8_t REG_VRMSTARGET = 0x5E;
    static const uint8_t REG_POWERTTARGET = 0x5F;
    static const uint8_t REG_BAUD = 0x60;
    static const uint8_t REG_VHYST = 0x63;
    static const uint8_t REG_ACDCV = 0x64;
    static const uint8_t REG_ACDCI = 0x65;
    static const uint8_t REG_VSURGETH = 0x67;
    static const uint8_t REG_VSAGTH = 0x68;
    static const uint8_t REG_VMINTH = 0x69;
    static const uint8_t REG_VMAXTH = 0x6A;
    static const uint8_t REG_VDROPTH = 0x6B;
    static const uint8_t REG_IMAXTH = 0x6C;
    static const uint8_t REG_PMAXTH = 0x6D;
    static const uint8_t REG_TMINTH = 0x6E;
    static const uint8_t REG_TMAXTH = 0x6F;
    static const uint8_t REG_FMINTH = 0x71;
    static const uint8_t REG_FMAXTH = 0x72;
    static const uint8_t REG_VDROPHOLD = 0x73;
    static const uint8_t REG_VMINHOLD = 0x74;
    static const uint8_t REG_IMAXHOLD = 0x75;
    static const uint8_t REG_PMAXHOLD = 0x76;
    static const uint8_t REG_TMINHOLD = 0x77;
    static const uint8_t REG_FMINHOLD = 0x78;
    static const uint8_t REG_TMINCNT = 0x79;
    static const uint8_t REG_TMAXCNT = 0x7A;
    static const uint8_t REG_VMINCNT = 0x7B;
    static const uint8_t REG_VMAXCNT = 0x7C;
    static const uint8_t REG_IMAXCNT = 0x7D;
    static const uint8_t REG_PMAXCNT = 0x7E;
    static const uint8_t REG_FMINCNT = 0x7F;
    static const uint8_t REG_FMAXCNT = 0x80;
    static const uint8_t REG_VSAGCNT = 0x81;
    static const uint8_t REG_VSURGECNT = 0x82;

    static const uint8_t SPI_READ_CMD = 0x40;
    static const uint8_t SPI_WRITE_CMD = 0x00;
};

#endif