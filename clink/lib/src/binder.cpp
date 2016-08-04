// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "binder.h"
#include "bind_resolver.h"

#include <core/base.h>
#include <core/str_hash.h>

#include <algorithm>

//------------------------------------------------------------------------------
template <int SIZE> static bool translate_chord(const char* chord, char (&out)[SIZE])
{
    // '\M-x'           = alt-x
    // '\C-x' or '^x'   = ctrl-x
    // '\e[t'           = ESC [ t (aka CSI t)
    // 'abc'            = abc

    int i = 0;
    for (; i < (SIZE - 1) && *chord; ++i, ++chord)
    {
        if (*chord != '\\' && *chord != '^')
        {
            out[i] = *chord;
            continue;
        }

        if (*chord == '^')
        {
            out[i] = *chord & 0x1f;
            continue;
        }

        ++chord;
        switch (*chord)
        {
        case '\0':
            i = SIZE;
            continue;

        case 'M':
            if (chord[1] != '-')
                return false;

            ++chord;
            out[i] = '\x1b';
            continue;

        case 'C':
            if (chord[1] != '-')
                return false;

            ++chord;
            out[i] = *chord & 0x1f;
            continue;

        // Some escape sequences for convenience.
        case 'e':   out[i] = '\x1b';    break;
        case 't':   out[i] = '\t';      break;
        case 'n':   out[i] = '\n';      break;
        case 'r':   out[i] = '\r';      break;
        case '0':   out[i] = '\0';      break;
        default:    out[i] = *chord;    break;
        }
    }

    out[i] = '\0';
    return true;
}



//------------------------------------------------------------------------------
binder::binder()
{
    // Initialise the default group.
    m_nodes[0] = { 1 };
    m_nodes[1] = { 0 };
    m_next_node = 2;

    static_assert(sizeof(node) == sizeof(group_node), "Size assumption");
}

//------------------------------------------------------------------------------
int binder::get_group(const char* name)
{
    if (name == nullptr || name[0] == '\0')
        return 1;

    unsigned int hash = str_hash(name);

    int index = get_group_node(0)->next;
    while (index)
    {
        const group_node* node = get_group_node(index);
        if (*(int*)(node->hash) == hash)
            return index;

        index = node->next;
    }

    return -1;
}

//------------------------------------------------------------------------------
int binder::create_group(const char* name)
{
    int index = alloc_nodes(2);
    if (index < 0)
        return -1;

    // Create a new group node;
    group_node* group = get_group_node(index);
    *(int*)(group->hash) = str_hash(name);
    group->is_group = 1;

    // Link the new node into the front of the list.
    group_node* master = get_group_node(0);
    group->next = master->next;
    master->next = index;

    // Next node along is the group's root.
    ++index;
    m_nodes[index] = {};
    return index;
}

//------------------------------------------------------------------------------
bool binder::bind(
    unsigned int group,
    const char* chord,
    editor_backend& backend,
    unsigned char id)
{
    // Validate input
    if (group >= sizeof_array(m_nodes))
        return false;

    const char* c = chord;
    while (*c)
        if (*c++ < 0)
            return false;

    // Translate from ASCII representation to actual keys.
    char translated[64];
    if (!translate_chord(chord, translated))
        return false;

    chord = translated;

    // Store the backend pointer
    int backend_index = add_backend(backend);
    if (backend_index < 0)
        return false;

    // Add the chord of keys into the node graph.
    int depth = 0;
    int head = group;
    for (; *chord; ++chord, ++depth)
        if (!(head = insert_child(head, *chord)))
            return false;

    --chord;

    // If the insert point is already bound we'll duplicate the node at the end
    // of the list. Also check if this is a duplicate of the existing bind.
    node* bindee = m_nodes + head;
    if (bindee->bound)
    {
        int check = head;
        while (check > head)
        {
            if (bindee->key == *chord && bindee->backend == backend_index && bindee->id == id)
                return true;

            check = bindee->next;
            bindee = m_nodes + check;
        }

        head = append(head, *chord);
    }

    if (!head)
        return false;

    bindee = m_nodes + head;
    bindee->backend = backend_index;
    bindee->bound = 1;
    bindee->depth = depth;
    bindee->id = id;

    return true;
}

//------------------------------------------------------------------------------
int binder::insert_child(int parent, unsigned char key)
{
    if (int child = find_child(parent, key))
        return child;

    return add_child(parent, key);
}

//------------------------------------------------------------------------------
int binder::find_child(int parent, unsigned char key) const
{
    const node* node = m_nodes + parent;

    int index = node->child;
    for (; index > parent; index = node->next)
    {
        node = m_nodes + index;
        if (node->key == key)
            return index;
    }

    return 0;
}

//------------------------------------------------------------------------------
int binder::add_child(int parent, unsigned char key)
{
    int child = alloc_nodes();
    if (child < 0)
        return 0;

    node addee = {};
    addee.key = key;

    int current_child = m_nodes[parent].child;
    if (current_child < parent)
    {
        addee.next = parent;
        m_nodes[parent].child = child;
    }
    else
    {
        int tail = find_tail(current_child);
        addee.next = parent;
        m_nodes[tail].next = child;
    }

    m_nodes[child] = addee;
    return child;
}

//------------------------------------------------------------------------------
int binder::find_tail(int head)
{
    while (m_nodes[head].next > head)
        head = m_nodes[head].next;

    return head;
}

//------------------------------------------------------------------------------
int binder::append(int head, unsigned char key)
{
    int index = alloc_nodes();
    if (index < 0)
        return 0;

    int tail = find_tail(head);

    node addee = {};
    addee.key = key;
    addee.next = m_nodes[tail].next;
    m_nodes[tail].next = index;

    m_nodes[index] = addee;
    return index;
}

//------------------------------------------------------------------------------
const binder::node& binder::get_node(unsigned int index) const
{
    if (index < sizeof_array(m_nodes))
        return m_nodes[index];

    static const node zero = {};
    return zero;
}

//------------------------------------------------------------------------------
binder::group_node* binder::get_group_node(unsigned int index)
{
    if (index < sizeof_array(m_nodes))
        return (group_node*)(m_nodes + index);

    return nullptr;
}

//------------------------------------------------------------------------------
int binder::alloc_nodes(unsigned int count)
{
    m_next_node += count;
    return (m_next_node < sizeof_array(m_nodes)) ? (m_next_node - count) : -1;
}

//------------------------------------------------------------------------------
int binder::add_backend(editor_backend& backend)
{
    for (int i = 0, n = m_backends.size(); i < n; ++i)
        if (*(m_backends[i]) == &backend)
            return i;

    if (editor_backend** slot = m_backends.push_back())
    {
        *slot = &backend;
        return int(slot - m_backends.front());
    }

    return -1;
}

//------------------------------------------------------------------------------
editor_backend* binder::get_backend(unsigned int index) const
{
    auto b = m_backends[index];
    return b ? *b : nullptr;
}
