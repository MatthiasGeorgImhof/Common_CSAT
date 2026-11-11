#ifndef CYPHAL_ADAPTER_API_HPP
#define CYPHAL_ADAPTER_API_HPP

#include <type_traits>
#include "cyphal.hpp" // Include your cyphal.hpp file

template <typename Adapter>
struct CyphalAdapterAPI {
    static constexpr bool has_cyphalTxPush = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::cyphalTxPush)>;
    static constexpr bool has_cyphalTxForward = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::cyphalTxForward)>;
    static constexpr bool has_cyphalRxSubscribe = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::cyphalRxSubscribe)>;
    static constexpr bool has_cyphalRxUnsubscribe = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::cyphalRxUnsubscribe)>;
    static constexpr bool has_cyphalRxReceive = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::cyphalRxReceive)>;
    static constexpr bool has_getNodeID = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::getNodeID)>;
    static constexpr bool has_setNodeID = std::is_member_function_pointer_v<decltype(&Cyphal<Adapter>::setNodeID)>;
};

template <typename Adapter>
inline constexpr bool checkCyphalAdapterAPI() {
    return CyphalAdapterAPI<Adapter>::has_cyphalTxPush &&
           CyphalAdapterAPI<Adapter>::has_cyphalTxForward &&
           CyphalAdapterAPI<Adapter>::has_cyphalRxSubscribe &&
           CyphalAdapterAPI<Adapter>::has_cyphalRxUnsubscribe &&
           CyphalAdapterAPI<Adapter>::has_cyphalRxReceive &&
           CyphalAdapterAPI<Adapter>::has_getNodeID &&
           CyphalAdapterAPI<Adapter>::has_setNodeID;
}

#endif // CYPHAL_ADAPTER_API_HPP