//
//  main.cpp
//  Slack code
//
//  Created by Julien Brinson on 8/18/16.
//  Copyright © 2016 Julien Brinson. All rights reserved.
//

#include <iostream>
#include <string>
#include <curl/curl.h>
#include "json/json.h"
#include <pthread.h>
#include <fstream>
#include <queue>
#include <stdio.h>
#include <semaphore.h>

const std::string APIURL = "https://slack.com/api/";
const int CHANNEL_NAME_MAX_LENGTH = 21;
pthread_mutex_t  output_lock, size_lock, map_lock;
const int BUFFER_SIZE = 20;
//sem_t *map_lock;
pthread_cond_t *full;

size_t retrieve_data_callback(char *data, size_t size, size_t nmemb, void *raw_json){
    ((std::string *)raw_json)->append(data, size*nmemb);
    return size*nmemb;
}

void call_slack(std::string slack_token, std::string methodName, std::string &payload, std::string param){
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, retrieve_data_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &payload);
    std::string request_url = APIURL + methodName +"?token=" + slack_token + param;
    curl_easy_setopt(curl, CURLOPT_URL, request_url.c_str());
    if(curl){
        curl_easy_perform(curl);
    }
    
    curl_easy_cleanup(curl);
}

Json::Value parse_payload(std::string &payload){
    Json::Value jObj;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse(payload, jObj);
    if(!parsingSuccessful){
        std::cout<< "failed to parse" << reader.getFormatedErrorMessages();
    }
    return jObj;
}

std::vector<std::string> extract_channels(std::string &payload, std::map<std::string, Json::Value> &j_channelMap){
    Json::Value jObj = parse_payload(payload);
    Json::Value j_channelList = jObj.get("channels", "default");
    std::vector<std::string> channels;
    int channel_index = 0;
    const char* channel_name = "name";
    while(j_channelList.isValidIndex(channel_index)){
        channels.push_back(j_channelList[channel_index][channel_name].asString());
        j_channelMap.insert(std::pair<std::string, Json::Value>(channels[channel_index], j_channelList[channel_index]));
        channel_index++;
    }
    return channels;
}

void cache_upkeep(const std::string content, std::map<std::string, Json::Value> &j_objMap){
    //create_cache if none
    Json::Value cached_data;
    Json::Reader reader;
    Json::Writer *writer = new Json::StyledWriter();
    pthread_mutex_lock(&output_lock);
    std::queue<std::string> cache_update;
    std::string file_path = "cache_"+ content + ".txt";
    char old_key[BUFFER_SIZE];
    std::fstream cache;
    
    cache.open(file_path, std::fstream::in);
    cache.getline(old_key, BUFFER_SIZE);
    cache.close();
    cache.clear();
    if(std::strlen(old_key) == 0){ //new file because it's empty
        //write_cache()
        pthread_mutex_lock(&size_lock);
        for(std::map<std::string, Json::Value>::iterator it = j_objMap.begin(); it != j_objMap.end(); ++it){
            
            std::string map_payload = it->first + ":\n"+ it->second.toStyledString() + "\n*break*\n"; //writer->write(it->second) + "\n";//
            cache_update.push(map_payload);
        }
        pthread_mutex_unlock(&size_lock);
    }else{
        
        cache.open(file_path, std::fstream::in);
//        std::string test;
//        std::fstream test_cache;
//        test_cache.open("test_cache.txt", std::fstream::out|std::fstream::trunc);
//        while(std::getline(cache, test)){
//            test_cache.write(test.c_str(), test.length());
//        }
//        test_cache.close();
        
        //cache.getline(old_key, BUFFER_SIZE);
        std::string old_Key;
        std::string file_value;
        std::string raw_json;
        std::getline(cache, old_Key);
        while(std::getline(cache, file_value)){
           
            if(!(file_value.compare("*break*")==0)){
                raw_json += file_value;
            }else{
                old_Key.pop_back();
                bool parseSuccessful = reader.parse(raw_json, cached_data);
                if(!parseSuccessful){
                    std::cout << reader.getFormatedErrorMessages();
                }
                
                std::string cache_payload;
                try{
                    if(cached_data.toStyledString().compare(j_objMap.at(old_Key).toStyledString()) == 0){
                        cache_payload = std::string(old_Key) + ":\n" + cached_data.toStyledString() + "\n*break*\n";
                        cache_update.push(cache_payload);
                    }else{
                        cache_payload = std::string(old_Key) + ":\n" + j_objMap.at(old_Key).toStyledString() + "\n*break*\n";
                        cache_update.push(cache_payload);
                    }
                }catch(const std::out_of_range &oor){
                    
                }
                std::getline(cache, old_Key);
            }
//            
//            size_t num_key_digits = std::strcspn(old_key, ":");
//            old_key[num_key_digits] = '\0';
//            
//            int peek = cache.peek();
//            
//            //reader.parse(cache, cached_data);
//            
//            //old key is not found in new map
//            std::getline(cache, old_Key);
            
            //cache.getline(old_key, BUFFER_SIZE);
        
        }
        
    }
    cache.close();
    cache.clear();
    cache.open(file_path, std::fstream::out|std::fstream::trunc);
    while(!cache_update.empty()){
        
        std::string map_payload = cache_update.front();
        cache.write(map_payload.c_str(), map_payload.length());
        cache_update.pop();
    }
    cache.close();
     //else check if content is updated and if not update contents
    pthread_mutex_unlock(&output_lock);
    delete writer;
}


std::vector<std::string> cache_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    
    std::ofstream channel_cache;
    channel_cache.open("cache/channels.txt");
    
    std::string payload = "";
    std::string methodName = "channels.list";
    std::string param = "";
    call_slack(slack_token, methodName, payload, param);
    std::vector<std::string> channels = extract_channels(payload, j_channelMap);
    cache_upkeep("channels", j_channelMap);
    
    return channels;
    
}

std::vector<std::string> get_channel_list(std::string slack_token, std::map<std::string, Json::Value> &j_channelMap){
    std::vector<std::string> channels = cache_channel_list(slack_token, j_channelMap);
    std::cout << "Channel List:";
    for(int channel_index = 0; channel_index<channels.size(); channel_index++){
        std::cout <<  "\n" << channel_index+1 << ":" << channels[channel_index];
    }
    std::cout << "\n";
    return channels;
}

bool isMessage(const Json::Value j_message){
    const char* type = "type";
    std::string message = "message";
    return message.compare(j_message[type].asString()) == 0;
}

std::string extract_text(Json::Value j_message){
    const char* text = "text";
    return j_message[text].asString();
}

std::vector<std::string> extract_messages(std::string &payload, std::string channel_id, std::map<std::string, Json::Value> &j_messageMap){
    std::vector<std::string> messages;
    Json::Value jObj = parse_payload(payload);
    const Json::Value j_messageList = jObj.get("messages", "default");
    pthread_mutex_lock(&map_lock);
    j_messageMap.insert(std::pair<std::string, Json::Value>(channel_id, j_messageList));
    pthread_mutex_unlock(&map_lock);
    //sem_post(map_lock);
    int messageIndex = 0;
    while(j_messageList.isValidIndex(messageIndex)){
        if(isMessage(j_messageList[messageIndex])){
            messages.push_back(extract_text(j_messageList[messageIndex]));
        }
        messageIndex++;
    }
    return messages;
}


std::string get_channel_id(Json::Value j_channel){
    std::string channel_id = "";
    const char* id = "id";
    channel_id = j_channel[id].asString(); //j_channel[id].toStyledString();
    return channel_id;
}

//std::vector<std::string> cache_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
//    std::string payload = "";
//    std::string methodName = "channels.history";
//    std::string channel_id = get_channel_id(j_channel);
//    std::string param = "&channel=" + channel_id;
//    call_slack(slack_token, methodName, payload, param);
//    
//    std::vector<std::string> messages = extract_messages(payload, channel_id, j_messageMap);
//    cache_upkeep("messages", j_messageMap);
//    
//    return messages;
//
//}
//
//void cache_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap ){
//        cache_message_history(slack_token, j_channelMap[channel], j_messageMap);
//}

std::vector<std::string> add_messageMap(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value> & j_messageMap){
    std::string payload = "";
    std::string methodName = "channels.history";
    std::string channel_id = get_channel_id(j_channel);
    std::string param = "&channel=" + channel_id;
    call_slack(slack_token, methodName, payload, param);
    std::vector<std::string> messages = extract_messages(payload, channel_id, j_messageMap);
    return messages;
}

void show_message_history(std::string slack_token, Json::Value j_channel, std::map<std::string, Json::Value>& j_messageMap){
    std::vector<std::string> messages = add_messageMap(slack_token, j_channel, j_messageMap);
    //cache_message_history(slack_token, j_channel, j_messageMap);
    std::cout << j_channel["name"].asString()<<" - Message History: \n";
    for(int message_index = 0; message_index < messages.size(); message_index++){
        std::cout << messages[message_index] << "\n";
    }
    cache_upkeep("messages", j_messageMap);
}

const void show_message_history_num(std::string slack_token, std::string channel, std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap ){
    show_message_history(slack_token, j_channelMap[channel], j_messageMap);
}

struct threadData{
    std::string slack_token;
    std::string channel_name;
    Json::Value channel;
    std::map<std::string, Json::Value> *j_channelMap_ptr;
    std::map<std::string, Json::Value> *j_messageMap_ptr;
    int num;
};

void* thread_show_history_num(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history_num(mydata->slack_token, mydata->channel_name, *(mydata->j_channelMap_ptr), *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}

void* thread_show_history(void* threadarg){
    struct threadData *mydata;
    mydata = (threadData*)threadarg;
    show_message_history(mydata->slack_token, mydata->channel, *(mydata->j_messageMap_ptr));
    pthread_exit(NULL);
    return NULL;
}



void select_channel(std::string slack_token, std::vector<std::string> channelList,  std::map<std::string, Json::Value> &j_channelMap, std::map<std::string, Json::Value> &j_messageMap){
    void * status;
    char selected[CHANNEL_NAME_MAX_LENGTH];
    
    std::cout << "select channel (number or name) for message history: ";
    std::cin >> selected;
    pthread_t threads[1];
    struct threadData td;
    td.slack_token = slack_token;
    
    td.j_channelMap_ptr = &j_channelMap;
    td.j_messageMap_ptr = &j_messageMap;
    pthread_mutex_lock(&size_lock);
    if(isdigit(selected[0])){
        int num;
        //converts string to int
        std::stringstream convert(selected);
        if(!(convert>> num)){
            num = 1;
        }
        num--;
        td.channel_name = channelList[num];
        int rc = pthread_create(&threads[0], NULL, thread_show_history_num, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(int i = 0; i <channelList.size(); i++){
            if(i != num){
                add_messageMap(slack_token, j_channelMap[channelList[i]], j_messageMap);
            }
        }
        
        //cache_message_history_num(slack_token, channelList[i], j_channelMap, j_messageMap);
        
    }else{
        td.channel = j_channelMap[selected];
        int rc = pthread_create(&threads[0], NULL, thread_show_history, (void*)&td);
        if(rc){
            std::cout << "couldn't create thread";
            exit(-1);
        }
        
        for(std::map<std::string, Json::Value>::iterator it = j_channelMap.begin(); it != j_channelMap.end(); ++it){
            if(std::strcmp(selected, it->first.c_str()) != 0){
                add_messageMap(slack_token, j_channelMap[it->first.c_str()], j_messageMap);
                //cache_message_history(slack_token, j_channelMap[it->first.c_str()], j_messageMap);
            }
        }
        
    }
    pthread_mutex_unlock(&size_lock);
    cache_upkeep("messages", j_messageMap);
    pthread_join(threads[0], &status);
}

int main(int argc, const char * argv[]) {
    // insert code here...
    /*Pseudo code to implement:
     1)intro output
     */
    pthread_mutex_init(&output_lock, NULL);
    pthread_mutex_init(&size_lock, NULL);
    pthread_mutex_init(&map_lock, NULL);
    std::string slack_token;
    std::cout << "Welcome to Slack!\n";
    std::cout << "Please provide token: ";
    
    std::cin >> slack_token;
    std::map<std::string, Json::Value> j_channelMap;
    std::map<std::string, Json::Value> j_messageMap;
     //2)get/list all available channels
    std::vector<std::string> channelList = get_channel_list(slack_token, j_channelMap);
    //int sem_num = -channelList.size() +1;
    //map_lock = sem_open("map_lock", O_CREAT, 0644, sem_num);
     //3)wait for select channel/show message history
    select_channel(slack_token, channelList, j_channelMap, j_messageMap);
//    std::cout << "final shabang!\n";
//    for(std::map<std::string, Json::Value>::iterator it=j_messageMap.begin(); it!=j_messageMap.end(); ++it){
//        std::cout << it->first << " : " << it->second << "\n";
//    }
//    
     //4)cache data
    pthread_mutex_destroy(&map_lock);
    pthread_mutex_destroy(&size_lock);
    pthread_mutex_destroy(&output_lock);
    //pthread_exit(NULL);

    return 0;
}
