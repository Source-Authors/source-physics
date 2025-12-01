#include <hk_base/base.h>
#include <hk_base/id_server/id_server.h>

// Initialize statics

hk_ID_Server hk_ID_Server::m_instance;

hk_ID_Server::hk_ID_Server() : m_next_id{0} {}
