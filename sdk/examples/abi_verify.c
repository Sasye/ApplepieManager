/* Compile the public ABI as C11, including callers using non-default packing. */
#pragma pack(push, 1)
#include "../applepie.h"
struct PackingRestored { char tag; void* value; };
#pragma pack(pop)
_Static_assert(sizeof(struct PackingRestored)==9,"caller packing restored");
_Static_assert(sizeof(APH_Manifest)==160,"legacy manifest unchanged");
_Static_assert(sizeof(APH_Api)==48,"legacy API prefix unchanged");
_Static_assert(sizeof(APH_Dependency)==88,"dependency ABI");
_Static_assert(sizeof(APH_PluginInfoData)==728,"plugin info ABI");
_Static_assert(sizeof(APH_ServiceDesc)==112,"service descriptor ABI");
_Static_assert(sizeof(APH_ServiceQuery)==144,"service query ABI");
_Static_assert(sizeof(APH_PluginServiceApi)==40,"service table ABI");
_Static_assert(sizeof(APH_ErrorDetail)==392,"diagnostic ABI");
_Static_assert(sizeof(APH_DiagnosticsApi)==24,"diagnostic table ABI");
_Static_assert(sizeof(APH_ConfigValue)==272,"config value ABI");
_Static_assert(sizeof(APH_ConfigOption)==64,"config option ABI");
_Static_assert(sizeof(APH_ConfigField)==616,"config field ABI");
_Static_assert(sizeof(APH_ConfigSchema)==16,"config schema ABI");
_Static_assert(sizeof(APH_ConfigSnapshot)==32,"config snapshot ABI");
_Static_assert(sizeof(APH_ConfigApi)==56,"config table ABI");
_Static_assert(offsetof(APH_ConfigApi,set_notifier)==APH_CONFIG_API_V1_PREFIX_BYTES,"config v1 prefix ABI");
