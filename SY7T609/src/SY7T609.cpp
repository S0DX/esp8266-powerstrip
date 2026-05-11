#include "SY7T609.h"

SY7T609::SY7T609() : _interfaceType(INTERFACE_SPI), _csPin(0), _baud(9600), _serial(nullptr), _maxRetries(3), _errorCount(0), _lastError(0) {
    _spiSettings = SPISettings(SY7T609_SPI_CLOCK, MSBFIRST, SY7T609_SPI_MODE);
}

SY7T609::~SY7T609() {
    // _serial 指向静态对象 Serial，不需要手动释放
    // 清理 SPI 资源
    if (isSPI()) {
        SPI.end();
    }
}

bool SY7T609::begin(SPISettings settings, uint8_t csPin) {
    return begin(settings, csPin, 3);
}

bool SY7T609::begin(SPISettings settings, uint8_t csPin, uint8_t maxRetries) {
    _interfaceType = INTERFACE_SPI;
    _spiSettings = settings;
    _csPin = csPin;
    _maxRetries = maxRetries;
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    SPI.begin();
    
    if (!verifyConnection()) {
        _errorCount++;
        return false;
    }
    return true;
}

bool SY7T609::begin(uint32_t baud) {
    return begin(baud, 3);
}

bool SY7T609::begin(uint32_t baud, uint8_t maxRetries) {
    _interfaceType = INTERFACE_UART;
    _baud = baud;
    _maxRetries = maxRetries;
    _serial = &Serial;
    _serial->begin(baud, SERIAL_8N1);
    delay(100);
    
    if (!verifyConnection()) {
        _errorCount++;
        return false;
    }
    return true;
}

bool SY7T609::verifyConnection() {
    uint32_t version;
    uint8_t retries = 0;
    
    while (retries < _maxRetries) {
        #ifdef ESP8266
        yield();
        #endif
        
        if (readRegister(REG_FW_VERSION, version, 1)) {
            if (version != 0 && version != 0xFFFFFF) {
                return true;
            }
        }
        retries++;
        delay(10);
    }
    
    _lastError = 2;
    return false;
}

uint32_t SY7T609::getLastError() {
    return _lastError;
}

void SY7T609::clearErrorCounter() {
    _errorCount = 0;
    _lastError = 0;
}

uint32_t SY7T609::getErrorCount() {
    return _errorCount;
}

bool SY7T609::readRegister(uint8_t addr, uint32_t& value) {
    return readRegister(addr, value, _maxRetries);
}

bool SY7T609::readRegister(uint8_t addr, uint32_t& value, uint8_t retries) {
    if (isSPI()) {
        uint8_t attempt = 0;
        while (attempt < retries) {
            #ifdef ESP8266
            yield();
            #endif
            
            value = spiTransfer(addr, 0, true);
            if (value != 0xFFFFFFFF && value != 0x000000) {
                return true;
            }
            attempt++;
            _errorCount++;
            _lastError = 3;
            if (attempt < retries) {
                delayMicroseconds(10);
            }
        }
        return false;
    } else {
        return uartCommand((addr << 8) | 0x01, value);
    }
}

bool SY7T609::readRegisterValidated(uint8_t addr, uint32_t& value, uint8_t retries) {
    bool success = readRegister(addr, value, retries);
    if (!success) {
        return false;
    }
    
    if (value == 0x000000 || value == 0xFFFFFF) {
        uint8_t verifyRetries = 0;
        while (verifyRetries < 3) {
            #ifdef ESP8266
            yield();
            #endif
            
            uint32_t verifyValue;
            if (readRegister(addr, verifyValue, 1) && verifyValue == value) {
                return true;
            }
            verifyRetries++;
            delayMicroseconds(50);
        }
        _errorCount++;
        _lastError = 7;
        return false;
    }
    
    return true;
}

bool SY7T609::writeRegister(uint8_t addr, uint32_t value) {
    return writeRegister(addr, value, _maxRetries);
}

bool SY7T609::writeRegister(uint8_t addr, uint32_t value, uint8_t retries) {
    if (isSPI()) {
        uint8_t attempt = 0;
        while (attempt < retries) {
            #ifdef ESP8266
            yield();
            #endif
            
            spiTransfer(addr, value, false);
            uint32_t readback;
            if (readRegister(addr, readback, 1) && (readback == value)) {
                return true;
            }
            attempt++;
            _errorCount++;
            _lastError = 4;
            if (attempt < retries) {
                delayMicroseconds(10);
            }
        }
        return false;
    } else {
        return uartCommand((addr << 8) | 0x00, value);
    }
}

uint32_t SY7T609::spiTransfer(uint8_t addr, uint32_t data, bool read) {
    uint8_t txBuffer[6];
    uint8_t rxBuffer[6];
    uint32_t result = 0;

    if (read) {
        txBuffer[0] = 0x01;
        txBuffer[1] = addr & 0x3F;
        txBuffer[2] = 0x02;
        txBuffer[3] = 0x00;
        txBuffer[4] = 0x00;
        txBuffer[5] = 0x00;
    } else {
        txBuffer[0] = 0x01;
        txBuffer[1] = addr & 0x3F;
        txBuffer[2] = 0x00;
        txBuffer[3] = (uint8_t)((data >> 16) & 0xFF);
        txBuffer[4] = (uint8_t)((data >> 8) & 0xFF);
        txBuffer[5] = (uint8_t)(data & 0xFF);
    }

    digitalWrite(_csPin, LOW);
    SPI.beginTransaction(_spiSettings);
    
    if (read) {
        SPI.transfer(txBuffer, 6);
        delayMicroseconds(12);
        SPI.transfer(rxBuffer, 6);
        result = ((uint32_t)rxBuffer[3] << 16) | ((uint32_t)rxBuffer[4] << 8) | rxBuffer[5];
    } else {
        SPI.transfer(txBuffer, 6);
    }
    
    SPI.endTransaction();
    digitalWrite(_csPin, HIGH);

    return result;
}

bool SY7T609::uartCommand(uint32_t cmd, uint32_t& response) {
    uint8_t txBuffer[7];
    uint8_t rxBuffer[7];

    txBuffer[0] = 0x01;
    txBuffer[1] = (uint8_t)((cmd >> 16) & 0xFF);
    txBuffer[2] = (uint8_t)((cmd >> 8) & 0xFF);
    txBuffer[3] = (uint8_t)(cmd & 0xFF);
    txBuffer[4] = (uint8_t)((response >> 16) & 0xFF);
    txBuffer[5] = (uint8_t)((response >> 8) & 0xFF);
    txBuffer[6] = (uint8_t)(response & 0xFF);

    while (_serial->available()) {
        _serial->read();
    }

    _serial->write(txBuffer, 7);
    _serial->flush();

    delay(20);

    int bytesRead = _serial->readBytes(rxBuffer, 7);
    if (bytesRead == 7) {
        response = ((uint32_t)rxBuffer[4] << 16) | ((uint32_t)rxBuffer[5] << 8) | rxBuffer[6];
        return true;
    }
    return false;
}

bool SY7T609::uartCommand(uint32_t cmd) {
    uint32_t response;
    return uartCommand(cmd, response);
}

bool SY7T609::writeCommand(Command cmd) {
    return writeRegister(REG_COMMAND, (uint32_t)cmd);
}

bool SY7T609::writeCalibrationCommand(CalibrationCommand cmd) {
    return writeRegister(REG_COMMAND, (uint32_t)cmd);
}

uint32_t SY7T609::readVRMS() {
    uint32_t value;
    readRegister(REG_VRMS, value);
    return value;
}

uint32_t SY7T609::readIRMS() {
    uint32_t value;
    readRegister(REG_IRMS, value);
    return value;
}

int32_t SY7T609::readPower() {
    uint32_t value;
    readRegister(REG_POWER, value);
    return (int32_t)((value << 8) >> 8);
}

int32_t SY7T609::readVAR() {
    uint32_t value;
    readRegister(REG_VAR, value);
    return (int32_t)((value << 8) >> 8);
}

int32_t SY7T609::readVA() {
    uint32_t value;
    readRegister(REG_VA, value);
    return (int32_t)((value << 8) >> 8);
}

int32_t SY7T609::readPF() {
    uint32_t value;
    readRegister(REG_PF, value);
    return (int32_t)((value << 8) >> 8);
}

uint32_t SY7T609::readFrequency() {
    uint32_t value;
    readRegister(REG_FREQUENCY, value);
    return value;
}

uint32_t SY7T609::readTemperature() {
    uint32_t value;
    readRegister(REG_CTEMP, value);
    return value;
}

int32_t SY7T609::readVAVG() {
    uint32_t value;
    readRegister(REG_VAVG, value);
    return (int32_t)((value << 8) >> 8);
}

int32_t SY7T609::readIAVG() {
    uint32_t value;
    readRegister(REG_IAVG, value);
    return (int32_t)((value << 8) >> 8);
}

uint32_t SY7T609::readDivisor() {
    uint32_t value;
    readRegister(REG_DIVISOR, value);
    return value;
}

uint64_t SY7T609::readFrame() {
    uint32_t frameLow, frameHigh;
    readRegister(REG_FRAME, frameLow);
    readRegister(REG_FRAME + 1, frameHigh);
    return ((uint64_t)frameHigh << 24) | frameLow;
}

int32_t SY7T609::readAvgPower() {
    uint32_t value;
    readRegister(REG_AVGPOWER, value);
    return (int32_t)((value << 8) >> 8);
}

SY7T609::MeasurementData SY7T609::readMeasurement() {
    MeasurementData data;
    data.vrms = readVRMS();
    data.irms = readIRMS();
    data.power = readPower();
    data.var = readVAR();
    data.va = readVA();
    data.pf = readPF();
    data.frequency = readFrequency();
    data.temperature = readTemperature();
    data.vavg = readVAVG();
    data.iavg = readIAVG();
    data.avgpower = readAvgPower();
    return data;
}

bool SY7T609::readMeasurementFast(MeasurementData& data) {
    if (!isSPI()) {
        return false;
    }
    
    #ifdef ESP8266
    yield();
    #endif
    
    uint8_t rxBuffer[21];
    
    digitalWrite(_csPin, LOW);
    SPI.beginTransaction(_spiSettings);
    
    uint8_t txBuffer[6];
    txBuffer[0] = 0x01;
    txBuffer[1] = REG_VRMS & 0x3F;
    txBuffer[2] = 0x02;
    txBuffer[3] = 0x00;
    txBuffer[4] = 0x00;
    txBuffer[5] = 0x00;
    
    SPI.transfer(txBuffer, 6);
    delayMicroseconds(12);
    SPI.transfer(rxBuffer, 21);
    
    SPI.endTransaction();
    digitalWrite(_csPin, HIGH);
    
    #ifdef ESP8266
    yield();
    #endif
    
    data.vrms = ((uint32_t)rxBuffer[0] << 16) | ((uint32_t)rxBuffer[1] << 8) | rxBuffer[2];
    data.irms = ((uint32_t)rxBuffer[3] << 16) | ((uint32_t)rxBuffer[4] << 8) | rxBuffer[5];
    data.power = (int32_t)(((uint32_t)rxBuffer[6] << 16) | ((uint32_t)rxBuffer[7] << 8) | rxBuffer[8]) << 8;
    data.power >>= 8;
    data.var = (int32_t)(((uint32_t)rxBuffer[9] << 16) | ((uint32_t)rxBuffer[10] << 8) | rxBuffer[11]) << 8;
    data.var >>= 8;
    data.va = (int32_t)(((uint32_t)rxBuffer[12] << 16) | ((uint32_t)rxBuffer[13] << 8) | rxBuffer[14]) << 8;
    data.va >>= 8;
    data.pf = (int32_t)(((uint32_t)rxBuffer[15] << 16) | ((uint32_t)rxBuffer[16] << 8) | rxBuffer[17]) << 8;
    data.pf >>= 8;
    data.frequency = ((uint32_t)rxBuffer[18] << 16) | ((uint32_t)rxBuffer[19] << 8) | rxBuffer[20];
    
    #ifdef ESP8266
    yield();
    #endif
    
    data.temperature = readTemperature();
    data.vavg = readVAVG();
    data.iavg = readIAVG();
    data.avgpower = readAvgPower();
    
    return true;
}

SY7T609::FundamentalData SY7T609::readFundamental() {
    FundamentalData data;
    readRegister(REG_VFUND, data.vfund);
    readRegister(REG_IFUND, data.ifund);
    uint32_t pfund;
    readRegister(REG_PFUND, pfund);
    data.pfund = (int32_t)pfund;
    uint32_t qfund;
    readRegister(REG_QFUND, qfund);
    data.qfund = (int32_t)qfund;
    uint32_t vafund;
    readRegister(REG_VAFUND, vafund);
    data.vafund = (int32_t)vafund;
    return data;
}

SY7T609::HarmonicData SY7T609::readHarmonic() {
    HarmonicData data;
    readRegister(REG_VHARM, data.vharm);
    readRegister(REG_IHARM, data.iharm);
    uint32_t pharm;
    readRegister(REG_PHARM, pharm);
    data.pharm = (int32_t)pharm;
    uint32_t qharm;
    readRegister(REG_QHARM, qharm);
    data.qharm = (int32_t)qharm;
    uint32_t vaharm;
    readRegister(REG_VAHARM, vaharm);
    data.vaharm = (int32_t)vaharm;
    return data;
}

bool SY7T609::readEnergyCounters(EnergyData& data) {
    bool success = true;
    success &= readRegister(REG_EPPCNT, data.eppcnt);
    success &= readRegister(REG_EPMCNT, data.epmcnt);
    success &= readRegister(REG_EPNCNT, data.epncnt);
    success &= readRegister(REG_EQNCNT, data.eqncnt);
    success &= readRegister(REG_ESNCNT, data.esncnt);
    return success;
}

bool SY7T609::clearEnergyCounters() {
    return writeCommand(CMD_CLEAR_ENERGY);
}

bool SY7T609::setBucketSize(uint32_t bucketH, uint32_t bucketL) {
    bool success = true;
    success &= writeRegister(REG_BUCKETH, bucketH);
    success &= writeRegister(REG_BUCKETL, bucketL);
    return success;
}

bool SY7T609::readCurrentMinMax(MinMaxData& data) {
    bool success = true;
    success &= readRegister(REG_ILO, data.lo);
    success &= readRegister(REG_IHI, data.hi);
    return success;
}

bool SY7T609::readVoltageMinMax(MinMaxData& data) {
    bool success = true;
    success &= readRegister(REG_VLO, data.lo);
    success &= readRegister(REG_VHI, data.hi);
    return success;
}

bool SY7T609::readPeaks(PeakData& data) {
    bool success = true;
    success &= readRegister(REG_VPEAK, data.vpeak);
    success &= readRegister(REG_IPEAK, data.ipeak);
    return success;
}

bool SY7T609::resetMinMax() {
    bool success = true;
    success &= writeRegister(REG_VHI, 0x000000);
    success &= writeRegister(REG_IHI, 0x000000);
    success &= writeRegister(REG_VLO, 0x7FFFFF);
    success &= writeRegister(REG_ILO, 0x7FFFFF);
    return success;
}

uint32_t SY7T609::readAlarms() {
    uint32_t value;
    readRegister(REG_ALARMS, value);
    return value;
}

bool SY7T609::setAlarmMask(DIOPin pin, uint32_t mask) {
    uint8_t regAddr;
    switch (pin) {
        case DIO_1: regAddr = REG_MASK1; break;
        case DIO_5: regAddr = REG_MASK5; break;
        case DIO_7: regAddr = REG_MASK7; break;
        case DIO_8: regAddr = REG_MASK8; break;
        default: return false;
    }
    return writeRegister(regAddr, mask);
}

uint32_t SY7T609::getAlarmCount(AlarmType type) {
    uint8_t regAddr;
    switch (type) {
        case ALARM_UNDERTEMP: regAddr = REG_TMINCNT; break;
        case ALARM_OVERTEMP: regAddr = REG_TMAXCNT; break;
        case ALARM_UNDERVOLT: regAddr = REG_VMINCNT; break;
        case ALARM_OVERVOLT: regAddr = REG_VMAXCNT; break;
        case ALARM_OVERCURRENT: regAddr = REG_IMAXCNT; break;
        case ALARM_OVERPOWER: regAddr = REG_PMAXCNT; break;
        case ALARM_UNDERFREQ: regAddr = REG_FMINCNT; break;
        case ALARM_OVERFREQ: regAddr = REG_FMAXCNT; break;
        case ALARM_VSAG: regAddr = REG_VSAGCNT; break;
        case ALARM_VSURGE: regAddr = REG_VSURGECNT; break;
        default: return 0;
    }
    uint32_t value;
    readRegister(regAddr, value);
    return value;
}

bool SY7T609::clearAlarm(AlarmType type) {
    return writeRegister(REG_ALARM_RESET, (1 << type));
}

bool SY7T609::setVoltageSagThreshold(uint32_t value) {
    return writeRegister(REG_VSAGTH, value);
}

bool SY7T609::setVoltageSurgeThreshold(uint32_t value) {
    return writeRegister(REG_VSURGETH, value);
}

bool SY7T609::setUnderVoltageThreshold(uint32_t value) {
    return writeRegister(REG_VMINTH, value);
}

bool SY7T609::setOverVoltageThreshold(uint32_t value) {
    return writeRegister(REG_VMAXTH, value);
}

bool SY7T609::setVoltageDropoutThreshold(uint32_t value) {
    return writeRegister(REG_VDROPTH, value);
}

bool SY7T609::setOverCurrentThreshold(uint32_t value) {
    return writeRegister(REG_IMAXTH, value);
}

bool SY7T609::setOverPowerThreshold(uint32_t value) {
    return writeRegister(REG_PMAXTH, value);
}

bool SY7T609::setUnderTempThreshold(int32_t value) {
    return writeRegister(REG_TMINTH, (uint32_t)value);
}

bool SY7T609::setOverTempThreshold(int32_t value) {
    return writeRegister(REG_TMAXTH, (uint32_t)value);
}

bool SY7T609::setUnderFreqThreshold(uint32_t value) {
    return writeRegister(REG_FMINTH, value);
}

bool SY7T609::setOverFreqThreshold(uint32_t value) {
    return writeRegister(REG_FMAXTH, value);
}

bool SY7T609::setVoltageHoldTime(uint32_t cycles) {
    return writeRegister(REG_VMINHOLD, cycles);
}

bool SY7T609::setCurrentHoldTime(uint32_t cycles) {
    return writeRegister(REG_IMAXHOLD, cycles);
}

bool SY7T609::setPowerHoldTime(uint32_t cycles) {
    return writeRegister(REG_PMAXHOLD, cycles);
}

bool SY7T609::setTemperatureHoldTime(uint32_t cycles) {
    return writeRegister(REG_TMINHOLD, cycles);
}

bool SY7T609::setFrequencyHoldTime(uint32_t cycles) {
    return writeRegister(REG_FMINHOLD, cycles);
}

bool SY7T609::waitForCalibrationComplete(uint32_t timeout) {
    uint32_t startTime = millis();
    uint32_t command;
    
    while (millis() - startTime < timeout) {
        #ifdef ESP8266
        yield();
        #endif
        
        if (readRegister(REG_COMMAND, command, 1)) {
            if (command == 0) {
                return true;
            }
        }
        delay(10);
    }
    
    _lastError = 5;
    return false;
}

bool SY7T609::calibrateVoltageGain(uint32_t vrmstarget) {
    if (!writeRegister(REG_VRMSTARGET, vrmstarget, 1)) {
        return false;
    }
    
    uint32_t verifyTarget;
    if (!readRegister(REG_VRMSTARGET, verifyTarget, 1) || verifyTarget != vrmstarget) {
        return false;
    }
    
    bool result = writeCalibrationCommand(CAL_V_GAIN);
    if (result) {
        delay(10);
        return waitForCalibrationComplete(5000);
    }
    return result;
}

bool SY7T609::calibrateCurrentGain(uint32_t irmstarget) {
    if (!writeRegister(REG_IRMSTARGET, irmstarget, 1)) {
        return false;
    }
    
    uint32_t verifyTarget;
    if (!readRegister(REG_IRMSTARGET, verifyTarget, 1) || verifyTarget != irmstarget) {
        return false;
    }
    
    bool result = writeCalibrationCommand(CAL_I_GAIN);
    if (result) {
        delay(10);
        return waitForCalibrationComplete(5000);
    }
    return result;
}

bool SY7T609::calibrateCurrentGainPower(uint32_t powertarget) {
    if (!writeRegister(REG_POWERTTARGET, powertarget, 1)) {
        return false;
    }
    
    uint32_t verifyTarget;
    if (!readRegister(REG_POWERTTARGET, verifyTarget, 1) || verifyTarget != powertarget) {
        return false;
    }
    
    bool result = writeCalibrationCommand(CAL_I_GAIN_PWR);
    if (result) {
        delay(10);
        return waitForCalibrationComplete(5000);
    }
    return result;
}

bool SY7T609::calibrateVoltageOffset() {
    bool result = writeCalibrationCommand(CAL_V_OFFSET);
    if (result) {
        return waitForCalibrationComplete(5000);
    }
    return result;
}

bool SY7T609::calibrateCurrentOffset() {
    bool result = writeCalibrationCommand(CAL_I_OFFSET);
    if (result) {
        return waitForCalibrationComplete(5000);
    }
    return result;
}

bool SY7T609::setCalibrationTargets(uint32_t vavgtarget, uint32_t iavgtarget, uint32_t vrmstarget, uint32_t irmstarget, uint32_t powertarget) {
    bool success = true;
    success &= writeRegister(REG_VAVGTARGET, vavgtarget);
    success &= writeRegister(REG_IAVGTARGET, iavgtarget);
    success &= writeRegister(REG_VRMSTARGET, vrmstarget);
    success &= writeRegister(REG_IRMSTARGET, irmstarget);
    success &= writeRegister(REG_POWERTTARGET, powertarget);
    return success;
}

bool SY7T609::configureDIO(DIOPin pin, uint8_t direction) {
    uint32_t currentDir;
    if (!readRegister(REG_DIO_DIR, currentDir, _maxRetries)) {
        return false;
    }
    uint8_t bit = (pin == DIO_5) ? 5 : (pin == DIO_7) ? 7 : (pin == DIO_8) ? 8 : pin;
    if (direction) {
        currentDir |= (1 << bit);
    } else {
        currentDir &= ~(1 << bit);
    }
    return writeRegister(REG_DIO_DIR, currentDir);
}

bool SY7T609::setDIOPolarity(DIOPin pin, uint8_t polarity) {
    uint32_t currentPol;
    if (!readRegister(REG_DIO_POL, currentPol, _maxRetries)) {
        return false;
    }
    uint8_t bit = (pin == DIO_5) ? 5 : (pin == DIO_7) ? 7 : (pin == DIO_8) ? 8 : pin;
    if (polarity) {
        currentPol |= (1 << bit);
    } else {
        currentPol &= ~(1 << bit);
    }
    return writeRegister(REG_DIO_POL, currentPol);
}

uint32_t SY7T609::readDIO() {
    uint32_t value;
    readRegister(REG_DIO_STATE, value);
    return value;
}

bool SY7T609::writeDIO(DIOPin pin, uint8_t value) {
    uint8_t bit = (pin == DIO_5) ? 5 : (pin == DIO_7) ? 7 : (pin == DIO_8) ? 8 : pin;
    uint32_t setMask = 0, resetMask = 0;
    if (value) {
        setMask = (1 << bit);
    } else {
        resetMask = (1 << bit);
    }
    bool success = true;
    if (setMask) {
        success &= writeRegister(REG_DIO_SET, setMask);
    }
    if (resetMask) {
        success &= writeRegister(REG_DIO_RST, resetMask);
    }
    return success;
}

bool SY7T609::setDIO(DIOPin pin) {
    return writeDIO(pin, 1);
}

bool SY7T609::clearDIO(DIOPin pin) {
    return writeDIO(pin, 0);
}

bool SY7T609::indirectRead(uint16_t addr, uint32_t& value) {
    bool success = true;
    success &= writeRegister(0x0B, (uint32_t)addr);
    uint32_t tempValue;
    success &= readRegister(0x0C, tempValue);
    value = tempValue;
    return success;
}

bool SY7T609::indirectWrite(uint16_t addr, uint32_t value) {
    bool success = true;
    success &= writeRegister(0x0A, (uint32_t)addr);
    success &= writeRegister(0x09, value);
    return success;
}

bool SY7T609::saveToFlash() {
    bool result = writeCommand(CMD_SAVE);
    if (result) {
        #ifdef ESP8266
        delay(10);
        yield();
        delay(40);
        yield();
        #else
        delay(50);
        #endif
        
        uint32_t command;
        if (readRegister(REG_COMMAND, command, 1) && command == 0) {
            return true;
        }
        _lastError = 6;
        return false;
    }
    return result;
}

bool SY7T609::loadFromFlash() {
    bool result = writeCommand(CMD_RESET);
    if (result) {
        delay(100);
        return verifyConnection();
    }
    return result;
}

bool SY7T609::clearFlash(uint8_t storage) {
    if (storage == 0) {
        return writeCommand((Command)0x000200);
    } else {
        return writeCommand((Command)0x000400);
    }
}

bool SY7T609::softReset() {
    return writeCommand(CMD_RESET);
}

bool SY7T609::setAccumulation(uint16_t accumCycles, uint32_t accum) {
    bool success = true;
    success &= writeRegister(REG_ACCUMCYC, accumCycles);
    success &= writeRegister(REG_ACCUM, accum);
    return success;
}

bool SY7T609::setHarm(uint8_t harmonic) {
    return writeRegister(REG_HARM, harmonic);
}

bool SY7T609::setVSCALE(uint32_t value) {
    return writeRegister(REG_VSCALE, value);
}

bool SY7T609::setISCALE(uint32_t value) {
    return writeRegister(REG_ISCALE, value);
}

bool SY7T609::setPSCALE(uint32_t value) {
    return writeRegister(REG_PSCALE, value);
}

bool SY7T609::setPFSCALE(uint32_t value) {
    return writeRegister(REG_PFSCALE, value);
}

bool SY7T609::setFSCALE(uint32_t value) {
    return writeRegister(REG_FSCALE, value);
}

bool SY7T609::setTSCALE(uint32_t value) {
    return writeRegister(REG_TSCALE, value);
}

bool SY7T609::setPhaseComp(int32_t value) {
    return writeRegister(REG_PHASECOMP, (uint32_t)value);
}

bool SY7T609::setHPF(uint8_t voltageHPF, uint8_t currentHPF) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (voltageHPF) {
        control |= (1 << 9);
    } else {
        control &= ~(1 << 9);
    }
    if (currentHPF) {
        control |= (1 << 8);
    } else {
        control &= ~(1 << 8);
    }
    return writeRegister(REG_CONTROL, control);
}

bool SY7T609::setAutoHPF(bool voltage, bool current) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (voltage) {
        control |= (1 << 12);
    } else {
        control &= ~(1 << 12);
    }
    if (current) {
        control |= (1 << 13);
    } else {
        control &= ~(1 << 13);
    }
    return writeRegister(REG_CONTROL, control);
}

bool SY7T609::setSwapVoltage(bool swap) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (swap) {
        control |= (1 << 14);
    } else {
        control &= ~(1 << 14);
    }
    return writeRegister(REG_CONTROL, control);
}

bool SY7T609::setSwapCurrent(bool swap) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (swap) {
        control |= (1 << 15);
    } else {
        control &= ~(1 << 15);
    }
    return writeRegister(REG_CONTROL, control);
}

bool SY7T609::setIShift(bool enable) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (enable) {
        control |= (1 << 18);
    } else {
        control &= ~(1 << 18);
    }
    return writeRegister(REG_CONTROL, control);
}

bool SY7T609::setPShift(bool enable) {
    uint32_t control;
    if (!readRegister(REG_CONTROL, control, _maxRetries)) {
        return false;
    }
    if (enable) {
        control |= (1 << 11);
    } else {
        control &= ~(1 << 11);
    }
    return writeRegister(REG_CONTROL, control);
}

uint32_t SY7T609::getFWVersion() {
    uint32_t version;
    readRegister(REG_FW_VERSION, version);
    return version;
}

uint32_t SY7T609::readControl() {
    uint32_t value;
    readRegister(REG_CONTROL, value);
    return value;
}

bool SY7T609::writeControl(uint32_t value) {
    return writeRegister(REG_CONTROL, value);
}