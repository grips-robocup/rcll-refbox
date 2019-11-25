#include "cap_station.h"

#include "mps_io_mapping.h"

#include <iostream>

namespace llsfrb {
#if 0
}
#endif
namespace mps_comm {
#if 0
}
#endif

CapStation::CapStation(std::string name, std::string ip, unsigned short port, ConnectionMode mode)
: Machine(name, Station::STATION_CAP, ip, port, mode)
{
}

CapStation::~CapStation()
{
}

void
CapStation::band_on_until_in()
{
	send_command(Operation::OPERATION_BAND_ON_UNTIL + machine_type_,
	             Operation::OPERATION_BAND_IN,
	             ConveyorDirection::BACKWARD);
}

void
CapStation::band_on_until_mid()
{
	send_command(Operation::OPERATION_BAND_ON_UNTIL + machine_type_,
	             Operation::OPERATION_BAND_MID,
	             ConveyorDirection::FORWARD);
}

void
CapStation::band_on_until_out()
{
	send_command(Operation::OPERATION_BAND_ON_UNTIL + machine_type_,
	             Operation::OPERATION_BAND_OUT,
	             ConveyorDirection::FORWARD);
}

void
CapStation::retrieve_cap()
{
	send_command(Operation::OPERATION_CAP_ACTION + machine_type_, Operation::OPERATION_CAP_RETRIEVE);
}

void
CapStation::mount_cap()
{
	send_command(Operation::OPERATION_CAP_ACTION + machine_type_, Operation::OPERATION_CAP_MOUNT);
}

void
CapStation::identify()
{
	send_command(Command::COMMAND_SET_TYPE, StationType::STATION_TYPE_CS);
}

} // namespace mps_comm
} // namespace llsfrb
