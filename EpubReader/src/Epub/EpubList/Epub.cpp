#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGE(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGD(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGW(tag, args...) \
  printf(args);                \
  printf("\n");
#endif
#include <map>
#include "../../tinyxml2/tinyxml2.h"
#include "../ZipFile/ZipFile.h"
#include "Epub.h"

static const char *TAG = "EPUB";

bool Epub::find_content_opf_file(ZipFile &zip, std::string &content_opf_file)
{
  // open up the meta data to find where the content.opf file lives
  char *meta_info = (char *)zip.read_file_to_memory("META-INF/container.xml");
  if (!meta_info)
  {
    ESP_LOGE(TAG, "Could not find META-INF/container.xml");
    return false;
  }
  // parse the meta data
  tinyxml2::XMLDocument meta_data_doc;
  auto result = meta_data_doc.Parse(meta_info);
  // finished with the data as it's been parsed
  free(meta_info);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Could not parse META-INF/container.xml");
    return false;
  }
  auto container = meta_data_doc.FirstChildElement("container");
  if (!container)
  {
    ESP_LOGE(TAG, "Could not find container element in META-INF/container.xml");
    return false;
  }
  auto rootfiles = container->FirstChildElement("rootfiles");
  if (!rootfiles)
  {
    ESP_LOGE(TAG, "Could not find rootfiles element in META-INF/container.xml");
    return false;
  }
  // find the root file that has the media-type="application/oebps-package+xml"
  auto rootfile = rootfiles->FirstChildElement("rootfile");
  while (rootfile)
  {
    const char *media_type = rootfile->Attribute("media-type");
    if (media_type && strcmp(media_type, "application/oebps-package+xml") == 0)
    {
      const char *full_path = rootfile->Attribute("full-path");
      if (full_path)
      {
        content_opf_file = full_path;
        return true;
      }
    }
    rootfile = rootfile->NextSiblingElement("rootfile");
  }
  ESP_LOGE(TAG, "Could not get path to content.opf file");
  return false;
}

bool Epub::parse_content_opf(ZipFile &zip, std::string &content_opf_file)
{
  // read in the content.opf file and parse it
  char *contents = (char *)zip.read_file_to_memory(content_opf_file.c_str());
  // parse the contents
  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(contents);
  free(contents);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Error parsing content.opf - %s", doc.ErrorIDToName(result));
    return false;
  }
  auto package = doc.FirstChildElement("package");
  if (!package)
  {
    ESP_LOGE(TAG, "Could not find package element in content.opf");
    return false;
  }
  // get the metadata - title and cover image
  auto metadata = package->FirstChildElement("metadata");
  if (!metadata)
  {
    ESP_LOGE(TAG, "Missing metadata");
    return false;
  }
  auto title = metadata->FirstChildElement("dc:title");
  if (!title)
  {
    ESP_LOGE(TAG, "Missing title");
    return false;
  }
  m_title = title->GetText();
  auto cover = metadata->FirstChildElement("meta");
  while (cover && cover->Attribute("name") && strcmp(cover->Attribute("name"), "cover") != 0)
  {
    cover = cover->NextSiblingElement("meta");
  }
  if (!cover)
  {
    ESP_LOGW(TAG, "Missing cover");
  }
  auto cover_item = cover ? cover->Attribute("content") : nullptr;
  // read the manifest and spine
  // the manifest gives us the names of the files
  // the spine gives us the order of the files
  // we can then read the files in the order they are in the spine
  auto manifest = package->FirstChildElement("manifest");
  if (!manifest)
  {
    ESP_LOGE(TAG, "Missing manifest");
    return false;
  }
  // create a mapping from id to file name
  auto item = manifest->FirstChildElement("item");
  std::map<std::string, std::string> items;
  bool found_ncx_media_type = false;

  while (item)
  {
    std::string item_id = item->Attribute("id");
    std::string href = m_base_path + item->Attribute("href");
    // grab the cover image
    if (cover_item && item_id == cover_item)
    {
      m_cover_image_item = href;
    }
    // grab the ncx file - prefer the NCX media type, fall back to the "ncx" id
    const char *media_type = item->Attribute("media-type");
    const char *properties = item->Attribute("properties");
    if (media_type && strcmp(media_type, "application/x-dtbncx+xml") == 0)
    {
      m_toc_ncx_item = href;
      found_ncx_media_type = true;
    }
    else if (item_id == "ncx" && !found_ncx_media_type)
    {
      m_toc_ncx_item = href;
    }
    // EPUB3 navigation document, used when there is no real NCX
    if (properties && strstr(properties, "nav"))
    {
      m_nav_item = href;
    }
    items[item_id] = href;
    item = item->NextSiblingElement("item");
  }
  // find the spine
  auto spine = package->FirstChildElement("spine");
  if (!spine)
  {
    ESP_LOGE(TAG, "Missing spine");
    return false;
  }
  // read the spine
  auto itemref = spine->FirstChildElement("itemref");
  while (itemref)
  {
    auto id = itemref->Attribute("idref");
    if (items.find(id) != items.end())
    {
      m_spine.push_back(std::make_pair(id, items[id]));
    }
    itemref = itemref->NextSiblingElement("itemref");
  }
  return true;
}

// Find the first child element whose name matches, ignoring any namespace
// prefix (some books use <ncx:ncx>, <ncx:navMap>, ...).
static tinyxml2::XMLElement *first_child_local(tinyxml2::XMLElement *parent, const char *name)
{
  for (auto e = parent->FirstChildElement(); e; e = e->NextSiblingElement())
  {
    const char *n = e->Name();
    const char *colon = strrchr(n, ':');
    if (strcmp(colon ? colon + 1 : n, name) == 0)
    {
      return e;
    }
  }
  return nullptr;
}

static tinyxml2::XMLElement *next_sibling_local(tinyxml2::XMLElement *e, const char *name)
{
  for (e = e->NextSiblingElement(); e; e = e->NextSiblingElement())
  {
    const char *n = e->Name();
    const char *colon = strrchr(n, ':');
    if (strcmp(colon ? colon + 1 : n, name) == 0)
    {
      return e;
    }
  }
  return nullptr;
}

// Recursively collect <a> links from an EPUB3 nav <ol>, flattening nesting.
static void collect_nav_links(tinyxml2::XMLElement *ol, const std::string &base_path, std::vector<EpubTocEntry> &toc)
{
  for (auto li = first_child_local(ol, "li"); li; li = next_sibling_local(li, "li"))
  {
    auto a = first_child_local(li, "a");
    if (a && a->Attribute("href"))
    {
      // the title may be wrapped in <span> etc, so gather all the text
      std::string title;
      for (auto n = a->FirstChild(); n; n = n->NextSibling())
      {
        if (n->ToText())
        {
          title += n->Value();
        }
        else if (n->ToElement() && n->ToElement()->GetText())
        {
          title += n->ToElement()->GetText();
        }
      }
      std::string href = base_path + a->Attribute("href");
      std::string anchor;
      size_t pos = href.find('#');
      if (pos != std::string::npos)
      {
        anchor = href.substr(pos + 1);
        href = href.substr(0, pos);
      }
      toc.push_back(EpubTocEntry(title, href, anchor, 0));
    }
    auto sub = first_child_local(li, "ol");
    if (sub)
    {
      collect_nav_links(sub, base_path, toc);
    }
  }
}

// Find the toc <nav> (epub:type="toc", else the first nav) below an element.
static tinyxml2::XMLElement *find_nav(tinyxml2::XMLElement *e)
{
  tinyxml2::XMLElement *first = nullptr;
  for (auto c = e->FirstChildElement(); c; c = c->NextSiblingElement())
  {
    const char *n = c->Name();
    const char *colon = strrchr(n, ':');
    if (strcmp(colon ? colon + 1 : n, "nav") == 0)
    {
      const char *type = c->Attribute("epub:type");
      if (type && strstr(type, "toc"))
      {
        return c;
      }
      if (!first)
      {
        first = c;
      }
    }
    else if (auto found = find_nav(c))
    {
      if (!first)
      {
        first = found;
      }
      const char *type = found->Attribute("epub:type");
      if (type && strstr(type, "toc"))
      {
        return found;
      }
    }
  }
  return first;
}

bool Epub::parse_toc_ncx_file(ZipFile &zip)
{
  // the ncx file should have been specified in the content.opf file,
  // EPUB3 books may only have a nav document
  if (m_toc_ncx_item.empty())
  {
    m_toc_ncx_item = m_nav_item;
  }
  if (m_toc_ncx_item.empty())
  {
    ESP_LOGE(TAG, "No ncx file specified");
    return false;
  }
  ESP_LOGI(TAG, "toc path: %s\n", m_toc_ncx_item.c_str());

  char *ncx_data = (char *)zip.read_file_to_memory(m_toc_ncx_item.c_str());
  if (!ncx_data)
  {
    ESP_LOGE(TAG, "Could not find %s", m_toc_ncx_item.c_str());
    return false;
  }
  // Parse the Toc contents
  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(ncx_data);
  free(ncx_data);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Error parsing toc %s", doc.ErrorIDToName(result));
    return false;
  }
  auto root = doc.RootElement();
  tinyxml2::XMLElement *ncx = nullptr;
  if (root)
  {
    const char *colon = strrchr(root->Name(), ':');
    const char *root_name = colon ? colon + 1 : root->Name();
    if (strcmp(root_name, "ncx") == 0)
    {
      ncx = root;
    }
    else if (strcmp(root_name, "html") == 0)
    {
      // EPUB3 navigation document instead of an NCX
      auto nav = find_nav(root);
      auto ol = nav ? first_child_local(nav, "ol") : nullptr;
      if (ol)
      {
        // links are relative to the nav document, not the opf
        std::string nav_base = m_toc_ncx_item.substr(0, m_toc_ncx_item.find_last_of('/') + 1);
        collect_nav_links(ol, nav_base, m_toc);
        return !m_toc.empty();
      }
    }
  }
  if (!ncx)
  {
    ESP_LOGE(TAG, "Could not find first child ncx in toc %s (root element: %s)",
             m_toc_ncx_item.c_str(), root ? root->Name() : "none");
    return false;
  }

  auto navMap = first_child_local(ncx, "navMap");
  if (!navMap)
  {
    ESP_LOGE(TAG, "Could not find navMap child in ncx");
    return false;
  }

  auto navPoint = first_child_local(navMap, "navPoint");

  // Fills toc_index map
  while (navPoint)
  {
    // navPoint has also an id & playOrder element: navPoint->Attribute("id");
    auto navLabel = first_child_local(first_child_local(navPoint, "navLabel"), "text")->FirstChild();
    std::string title = navLabel->Value();
    auto content = first_child_local(navPoint, "content");
    std::string href = m_base_path + content->Attribute("src");
    // split the href on the # to get the href and the anchor
    size_t pos = href.find('#');
    std::string anchor = "";

    if (pos != std::string::npos)
    {
      anchor = href.substr(pos + 1);
      href = href.substr(0, pos);
    }
    m_toc.push_back(EpubTocEntry(title, href, anchor, 0));
    // ESP_LOGI(TAG, "%s -> %s#%s", title.c_str(), href.c_str(), anchor.c_str());
    navPoint = next_sibling_local(navPoint, "navPoint");
  }
  return true;
}

Epub::Epub(const std::string &path) : m_path(path)
{
}

// load in the meta data for the epub file
bool Epub::load()
{
  ZipFile zip(m_path.c_str());
  std::string content_opf_file;
  if (!find_content_opf_file(zip, content_opf_file))
  {
    #ifndef UNIT_TEST
      #ifdef USE_SPIFFS
        ESP_LOGI(TAG, "Is SPIFFs data uploaded?\npio run -t uploadfs");
      #endif
      ESP_LOGE(TAG, "Could not open ePub. Restarting in 10 secs.");
      vTaskDelay(pdMS_TO_TICKS(1000*10));
      esp_restart();
    #endif
    return false;
  }
  // get the base path for the content
  m_base_path = content_opf_file.substr(0, content_opf_file.find_last_of('/') + 1);
  if (!parse_content_opf(zip, content_opf_file))
  {
    return false;
  }
  if (!parse_toc_ncx_file(zip))
  {
    return false;
  }
  return true;
}

const std::string &Epub::get_title()
{
  return m_title;
}

const std::string &Epub::get_cover_image_item()
{
  return m_cover_image_item;
}

std::string normalise_path(const std::string &path)
{
  std::vector<std::string> components;
  std::string component;
  for (auto c : path)
  {
    if (c == '/')
    {
      if (!component.empty())
      {
        if (component == "..")
        {
          if (!components.empty())
          {
            components.pop_back();
          }
        }
        else
        {
          components.push_back(component);
        }
        component.clear();
      }
    }
    else
    {
      component += c;
    }
  }
  if (!component.empty())
  {
    components.push_back(component);
  }
  std::string result;
  for (auto &component : components)
  {
    if (result.size() > 0)
    {
      result += "/";
    }
    result += component;
  }
  return result;
}

uint8_t *Epub::get_item_contents(const std::string &item_href, size_t *size)
{
  ZipFile zip(m_path.c_str());
  std::string path = normalise_path(item_href);
  auto content = zip.read_file_to_memory(path.c_str(), size);
  if (!content)
  {
    ESP_LOGE(TAG, "Failed to read item %s", path.c_str());
    return nullptr;
  }
  return content;
}

int Epub::get_spine_items_count()
{
  return m_spine.size();
}

std::string &Epub::get_spine_item(int spine_index)
{
  // exceptions are disabled in the Arduino build, so bounds check instead
  if (spine_index < 0 || spine_index >= (int)m_spine.size())
  {
    ESP_LOGI(TAG, "get_spine_item index:%d is out_of_range", spine_index);
    spine_index = 0;
  }
  return m_spine.at(spine_index).second;
}

EpubTocEntry &Epub::get_toc_item(int toc_index)
{
  return m_toc[toc_index];
}

int Epub::get_toc_items_count()
{
  return m_toc.size();
}

// work out the section index for a toc index
int Epub::get_spine_index_for_toc_index(int toc_index)
{
  // the toc entry should have an href that matches the spine item
  // so we can find the spine index by looking for the href
  for (int i = 0; i < m_spine.size(); i++)
  {
    if (m_spine[i].second == m_toc[toc_index].href)
    {
      return i;
    }
  }
  ESP_LOGI(TAG, "Section not found");
  // not found - default to the start of the book
  return 0;
}
