/**
 * src/common/model/element.cc
 *
 * Copyright (c) 2021-2023 Bartek Kryza <bkryza@gmail.com>
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

#include "element.h"

#include "util/util.h"

#include <ostream>
#include <utility>

namespace clanguml::common::model {

element::element(namespace_ using_namespace)
    : using_namespace_{std::move(using_namespace)}
{
}

void element::set_using_namespaces(const namespace_ &un)
{
    using_namespace_ = un;
}

const namespace_ &element::using_namespace() const { return using_namespace_; }

inja::json element::context() const
{
    inja::json ctx;
    ctx["name"] = name();
    ctx["type"] = type_name();
    ctx["alias"] = alias();
    ctx["full_name"] = full_name(false);
    ctx["namespace"] = get_namespace().to_string();
    if (comment().has_value()) {
        ctx["comment"] = comment().value();
    }

    return ctx;
}

bool operator==(const element &l, const element &r)
{
    return l.full_name(false) == r.full_name(false);
}

std::ostream &operator<<(std::ostream &out, const element &rhs)
{
    out << "(" << rhs.name() << ", ns=[" << rhs.get_namespace().to_string()
        << "], full_name=[" << rhs.full_name(true) << "])";

    return out;
}

} // namespace clanguml::common::model
