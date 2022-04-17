/**
 * src/common/model/diagram_filter.cc
 *
 * Copyright (c) 2021-2022 Bartek Kryza <bkryza@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "diagram_filter.h"

namespace clanguml::common::model {

filter_visitor::filter_visitor(filter_t type)
    : type_{type}
{
}

tvl::value_t filter_visitor::match(
    const diagram &d, const common::model::element &e) const
{
    return {};
}

tvl::value_t filter_visitor::match(
    const diagram &d, const common::model::relationship_t &r) const
{
    return {};
}

tvl::value_t filter_visitor::match(
    const diagram &d, const common::model::access_t &a) const
{
    return {};
}

tvl::value_t filter_visitor::match(
    const diagram &d, const common::model::namespace_ &ns) const
{
    return {};
}

tvl::value_t filter_visitor::match(
    const diagram &d, const common::model::source_file &f) const
{
    return {};
}

bool filter_visitor::is_inclusive() const
{
    return type_ == filter_t::kInclusive;
}

bool filter_visitor::is_exclusive() const
{
    return type_ == filter_t::kExclusive;
}

filter_t filter_visitor::type() const { return type_; }

anyof_filter::anyof_filter(
    filter_t type, std::vector<std::unique_ptr<filter_visitor>> filters)
    : filter_visitor{type}
    , filters_{std::move(filters)}
{
}

tvl::value_t anyof_filter::match(
    const diagram &d, const common::model::element &e) const
{
    return tvl::any_of(filters_.begin(), filters_.end(),
        [&d, &e](const auto &f) { return f->match(d, e); });
}

namespace_filter::namespace_filter(
    filter_t type, std::vector<namespace_> namespaces)
    : filter_visitor{type}
    , namespaces_{namespaces}
{
}

tvl::value_t namespace_filter::match(
    const diagram &d, const namespace_ &ns) const
{
    if (ns.is_empty())
        return {};

    return tvl::any_of(namespaces_.begin(), namespaces_.end(),
        [&ns](const auto &nsit) { return ns.starts_with(nsit) || ns == nsit; });
}

tvl::value_t namespace_filter::match(const diagram &d, const element &e) const
{
    if (dynamic_cast<const package *>(&e) != nullptr) {
        return tvl::any_of(
            namespaces_.begin(), namespaces_.end(), [&e](const auto &nsit) {
                return (e.get_namespace() | e.name()).starts_with(nsit) ||
                    nsit.starts_with(e.get_namespace() | e.name()) ||
                    (e.get_namespace() | e.name()) == nsit;
            });
    }
    else {
        return tvl::any_of(
            namespaces_.begin(), namespaces_.end(), [&e](const auto &nsit) {
                return e.get_namespace().starts_with(nsit);
            });
    }
}

element_filter::element_filter(filter_t type, std::vector<std::string> elements)
    : filter_visitor{type}
    , elements_{elements}
{
}

tvl::value_t element_filter::match(const diagram &d, const element &e) const
{
    return tvl::any_of(elements_.begin(), elements_.end(),
        [&e](const auto &el) { return e.full_name(false) == el; });
}

subclass_filter::subclass_filter(filter_t type, std::vector<std::string> roots)
    : filter_visitor{type}
    , roots_{roots}
{
}

tvl::value_t subclass_filter::match(const diagram &d, const element &e) const
{
    if (d.type() != diagram_t::kClass)
        return {};

    if (roots_.empty())
        return {};

    if (!d.complete())
        return {};

    const auto &cd = dynamic_cast<const class_diagram::model::diagram &>(d);

    // First get all parents of element e
    std::unordered_set<
        type_safe::object_ref<const class_diagram::model::class_, false>>
        parents;

    const auto &fn = e.full_name(false);
    auto class_ref = cd.get_class(fn);

    if (!class_ref.has_value())
        return false;

    parents.emplace(class_ref.value());

    cd.get_parents(parents);

    // Now check if any of the parents matches the roots specified in the
    // filter config
    for (const auto &root : roots_) {
        for (const auto &parent : parents) {
            if (root == parent.get().full_name(false))
                return true;
        }
    }

    return false;
}

specialization_filter::specialization_filter(
    filter_t type, std::vector<std::string> roots)
    : filter_visitor{type}
    , roots_{roots}
{
}

void specialization_filter::init(const class_diagram::model::diagram &cd) const
{
    if (initialized_)
        return;

    // First get all templates specified in the configuration
    for (const auto &template_root : roots_) {
        auto template_ref = cd.get_class(template_root);
        if (template_ref.has_value())
            templates_.emplace(template_ref.value());
    }

    // Iterate over the templates set, until no new template instantiations or
    // specializations are found
    bool found_new_template{true};
    while (found_new_template) {
        found_new_template = false;
        for (const auto &t : cd.classes()) {
            auto tfn = t->full_name(false);
            auto tfn_relative = t->full_name(true);
            for (const auto &r : t->relationships()) {
                if (r.type() == relationship_t::kInstantiation) {
                    auto r_dest = r.destination();
                    for (const auto &t_dest : templates_) {
                        auto t_dest_full = t_dest->full_name(true);
                        if (r_dest == t_dest_full) {
                            auto inserted = templates_.insert(t);
                            if (inserted.second)
                                found_new_template = true;
                        }
                    }
                }
            }
        }
    }

    initialized_ = true;
}

tvl::value_t specialization_filter::match(
    const diagram &d, const element &e) const
{
    if (d.type() != diagram_t::kClass)
        return {};

    if (roots_.empty())
        return {};

    if (!d.complete())
        return {};

    const auto &cd = dynamic_cast<const class_diagram::model::diagram &>(d);

    init(cd);

    const auto &fn = e.full_name(false);
    auto class_ref = cd.get_class(fn);

    if (!class_ref.has_value())
        // Couldn't find the element in the diagram
        return false;

    // Now check if the e element is contained in the calculated set
    const auto &e_full_name = e.full_name(true);
    bool res = std::find_if(templates_.begin(), templates_.end(),
                   [&e_full_name](const auto &te) {
                       return te->full_name(true) == e_full_name;
                   }) != templates_.end();

    return res;
}

relationship_filter::relationship_filter(
    filter_t type, std::vector<relationship_t> relationships)
    : filter_visitor{type}
    , relationships_{relationships}
{
}

tvl::value_t relationship_filter::match(
    const diagram &d, const relationship_t &r) const
{
    return tvl::any_of(relationships_.begin(), relationships_.end(),
        [&r](const auto &rel) { return r == rel; });
}

access_filter::access_filter(filter_t type, std::vector<access_t> access)
    : filter_visitor{type}
    , access_{access}
{
}

tvl::value_t access_filter::match(const diagram &d, const access_t &a) const
{
    return tvl::any_of(access_.begin(), access_.end(),
        [&a](const auto &access) { return a == access; });
}

context_filter::context_filter(filter_t type, std::vector<std::string> context)
    : filter_visitor{type}
    , context_{context}
{
}

tvl::value_t context_filter::match(const diagram &d, const element &e) const
{
    if (d.type() != diagram_t::kClass)
        return {};

    if (!d.complete())
        return {};

    return tvl::any_of(context_.begin(), context_.end(),
        [&e, &d](const auto &context_root_name) {
            const auto &context_root =
                static_cast<const class_diagram::model::diagram &>(d).get_class(
                    context_root_name);

            if (context_root.has_value()) {
                // This is a direct match to the context root
                if (context_root.value().full_name(false) == e.full_name(false))
                    return true;

                // Return a positive match if the element e is in a direct
                // relationship with any of the context_root's
                for (const relationship &rel :
                    context_root.value().relationships()) {
                    if (rel.destination() == e.full_name(false))
                        return true;
                }
                for (const relationship &rel : e.relationships()) {
                    if (rel.destination() ==
                        context_root.value().full_name(false))
                        return true;
                }

                // Return a positive match if the context_root is a parent
                // of the element
                for (const class_diagram::model::class_parent &p :
                    context_root.value().parents()) {
                    if (p.name() == e.full_name(false))
                        return true;
                }
                if (dynamic_cast<const class_diagram::model::class_ *>(&e) !=
                    nullptr) {
                    for (const class_diagram::model::class_parent &p :
                        static_cast<const class_diagram::model::class_ &>(e)
                            .parents()) {
                        if (p.name() == context_root.value().full_name(false))
                            return true;
                    }
                }
            }

            return false;
        });
}

paths_filter::paths_filter(filter_t type, const std::filesystem::path &root,
    std::vector<std::filesystem::path> p)
    : filter_visitor{type}
    , root_{root}
{
    for (auto &&path : p) {
        std::filesystem::path absolute_path;

        if (path.is_relative())
            absolute_path = root / path;

        absolute_path = absolute_path.lexically_normal();

        paths_.emplace_back(std::move(absolute_path));
    }
}

tvl::value_t paths_filter::match(
    const diagram &d, const common::model::source_file &p) const
{
    if (paths_.empty()) {
        return {};
    }

    auto pp = p.fs_path(root_);
    for (const auto &path : paths_) {
        if (util::starts_with(pp, path))
            return true;
    }

    return false;
}

diagram_filter::diagram_filter(
    const common::model::diagram &d, const config::diagram &c)
    : diagram_{d}
{
    init_filters(c);
}

void diagram_filter::add_inclusive_filter(std::unique_ptr<filter_visitor> fv)
{
    inclusive_.emplace_back(std::move(fv));
}

void diagram_filter::add_exclusive_filter(std::unique_ptr<filter_visitor> fv)
{
    exclusive_.emplace_back(std::move(fv));
}

bool diagram_filter::should_include(
    namespace_ ns, const std::string &name) const
{
    if (should_include(ns)) {
        element e{namespace_{}};
        e.set_name(name);
        e.set_namespace(ns);

        return should_include(e);
    }

    return false;
}

void diagram_filter::init_filters(const config::diagram &c)
{
    // Process inclusive filters
    if (c.include) {
        add_inclusive_filter(std::make_unique<namespace_filter>(
            filter_t::kInclusive, c.include().namespaces));
        add_inclusive_filter(std::make_unique<relationship_filter>(
            filter_t::kInclusive, c.include().relationships));
        add_inclusive_filter(std::make_unique<access_filter>(
            filter_t::kInclusive, c.include().access));
        add_inclusive_filter(std::make_unique<paths_filter>(
            filter_t::kInclusive, c.base_directory(), c.include().paths));

        // Include any of these matches even if one them does not match
        std::vector<std::unique_ptr<filter_visitor>> element_filters;
        element_filters.emplace_back(std::make_unique<element_filter>(
            filter_t::kInclusive, c.include().elements));
        element_filters.emplace_back(std::make_unique<subclass_filter>(
            filter_t::kInclusive, c.include().subclasses));
        element_filters.emplace_back(std::make_unique<specialization_filter>(
            filter_t::kInclusive, c.include().specializations));
        element_filters.emplace_back(std::make_unique<context_filter>(
            filter_t::kInclusive, c.include().context));

        add_inclusive_filter(std::make_unique<anyof_filter>(
            filter_t::kInclusive, std::move(element_filters)));
    }

    // Process exclusive filters
    if (c.exclude) {
        add_exclusive_filter(std::make_unique<namespace_filter>(
            filter_t::kExclusive, c.exclude().namespaces));
        add_exclusive_filter(std::make_unique<paths_filter>(
            filter_t::kExclusive, c.base_directory(), c.exclude().paths));
        add_exclusive_filter(std::make_unique<element_filter>(
            filter_t::kExclusive, c.exclude().elements));
        add_exclusive_filter(std::make_unique<relationship_filter>(
            filter_t::kExclusive, c.exclude().relationships));
        add_exclusive_filter(std::make_unique<access_filter>(
            filter_t::kExclusive, c.exclude().access));
        add_exclusive_filter(std::make_unique<subclass_filter>(
            filter_t::kExclusive, c.exclude().subclasses));
        add_exclusive_filter(std::make_unique<specialization_filter>(
            filter_t::kExclusive, c.exclude().specializations));
        add_exclusive_filter(std::make_unique<context_filter>(
            filter_t::kExclusive, c.exclude().context));
    }
}

template <>
bool diagram_filter::should_include<std::string>(const std::string &name) const
{
    auto [ns, n] = cx::util::split_ns(name);

    return should_include(ns, n);
}
}
