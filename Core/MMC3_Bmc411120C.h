#pragma once
#include "stdafx.h"
#include "MMC3.h"

class MMC3_Bmc411120C : public MMC3
{
private:
	uint8_t _exReg;
	uint8_t _dipSwitch;

protected:
	void InitMapper() override
	{
		AddRegisterRange(0x6000, 0xFFFF, MemoryOperation::Write);
		_exReg = 0;
		_dipSwitch = 0x04;

		MMC3::InitMapper();
	}

	void StreamState(bool saving) override
	{
		MMC3::StreamState(saving);
		Stream(_exReg, _dipSwitch);
	}

	void Reset(bool softReset) override 
	{
		BaseMapper::Reset(softReset);
		_dipSwitch ^= 0x04;
	}

	void SelectCHRPage(uint16_t slot, uint16_t page, ChrMemoryType memoryType = ChrMemoryType::Default) override
	{
		MMC3::SelectCHRPage(slot, page | ((_exReg & 0x03) << 7), memoryType);
	}

	void SelectPRGPage(uint16_t slot, uint16_t page, PrgMemoryType memoryType = PrgMemoryType::PrgRom) override
	{
		if(_exReg & (0x08 | _dipSwitch)) {
			MMC3::SelectPrgPage4x(0, (((_exReg >> 4) & 0x03) | 0x0C) << 2);
		} else {
			MMC3::SelectPRGPage(slot, (page & 0x0F) | ((_exReg & 0x03) << 4));
		}
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		if(addr < 0x8000) {
			_exReg = (uint8_t)addr;
			UpdateState();
		} else {
			MMC3::WriteRegister(addr, value);
		}
	}
};