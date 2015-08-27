<?php

class NETProcessor extends FeedsNodeProcessor
{
  /** @var  array */
  protected $languages;

  /** @var string */
  protected $neutralLanguageKey = 'und';

  /** @var array */
  protected $fieldInfo = [];

  /** @var  string */
  private $currentItemLanguageKey;


  /**
   * @param string $id
   */
  public function __construct($id) {
    parent::__construct($id);
    $this->setAvailableLanguages();
  }

  /**
   * @return mixed
   */
  public function entityType() {
    return parent::entityType();
  }

  /**
   * @param \FeedsSource $source
   * @return mixed
   */
  protected function newEntity(FeedsSource $source) {
    $node = new stdClass();
    $node->type = $this->bundle();
    $node->changed = REQUEST_TIME;
    $node->created = REQUEST_TIME;
    $node->language = $this->getDefaultLanguageForNodeType($node->type);
    $node->is_new = TRUE;
    node_object_prepare($node);
    $node->log = 'Created by ' . __CLASS__;
    $node->uid = $this->config['author'];
    return $node;
  }

  /**
   * @param \FeedsSource $source
   * @param integer $nid
   * @return mixed
   */
  protected function entityLoad(FeedsSource $source, $nid) {
    $entity = parent::entityLoad($source, $nid);
    //dpm($entity, "LOADED ENTITY");
    return $entity;
  }

  /**
   * @param $entity
   * @throws \Exception
   */
  public function entitySave($entity) {

    if(isset($entity->is_new) && $entity->is_new && $entity->language != $this->currentItemLanguageKey) {
      throw new \Exception(
        t('['.$entity->title.'] - '
          . 'You are trying to create a new node('.$entity->type.') in the wrong language! '
          . 'The default language for this node type is: "' . $entity->language . '" '
          . 'but the current line item language is: "' . $this->currentItemLanguageKey . '"! '
          . 'Make sure to create the node in the correct language first.'
        )
      );
    }

    if($this->currentItemLanguageKey != $entity->language) {
      //check that field_title is set for current language
      if (!isset($entity->title_field[$this->currentItemLanguageKey])) {
        $entity->title_field[$this->currentItemLanguageKey] = [['value'=>$entity->title]];
      }
      $translationHandler = entity_translation_get_handler('node', $entity);
      $translation = [
        'translate' => 0,
        'status' => 1,
        'language' => $this->currentItemLanguageKey,
        'source' => $entity->language,
      ];
      $translationHandler->setTranslation($translation, $entity);
    }
    //dpm($entity, "SAVING ENTITY(".$this->currentItemLanguageKey.")");
    node_save($entity);
  }

  public function process(FeedsSource $source, FeedsParserResult $parser_result) {
    parent::process($source, $parser_result);
  }

  /**
   * @param \FeedsSource       $source
   * @param \FeedsParserResult $result
   * @param \stdClass          $target_item
   * @return array|null
   * @throws \Exception
   */
  protected function map(FeedsSource $source, FeedsParserResult $result, $target_item = NULL) {
    $currentItem = $result->currentItem();
    if(!isset($currentItem[$this->config["source_language_key"]])) {
      throw new \Exception(
        t('Your configuration "Source Language Column" is set to: "%source_language_key" '
        . ' but your source does not have that column!'
        . ' <br />Available columns: %available_columns'
        , [
            '%source_language_key' => $this->config["source_language_key"],
            '%available_columns' => json_encode(array_keys($currentItem)),
          ])
      );
    }

    if(!in_array($currentItem[$this->config["source_language_key"]], array_keys($this->languages))) {
      throw new \Exception(
        t('The current item is set to use language: "%language_key" '
          . ' but this language is not available on your site!'
          . ' <br />Available languages: %available_languages'
          , [
            '%language_key' => $currentItem[$this->config["source_language_key"]],
            '%available_languages' => '(' . implode("|", array_keys($this->languages)) . ')',
          ])
      );
    }

    $this->currentItemLanguageKey = $currentItem[$this->config["source_language_key"]];

    return parent::map($source, $result, $target_item);
  }

  /**
   * Maps values onto the target item.
   * @param \FeedsSource $source
   * @param string       $target
   * @param mixed        $target_item
   * @param mixed        $value
   * @param array        $mapping
   */
  protected function mapToTarget(FeedsSource $source, $target, &$target_item, $value, array $mapping) {
    $targets = $this->getCachedTargets();
    if (isset($targets[$target]['preprocess_callbacks'])) {
      foreach ($targets[$target]['preprocess_callbacks'] as $callback) {
        call_user_func_array($callback, array($source, $target_item, $target, &$mapping));
      }
    }

    // These callbacks do NOT know how do do Entity Translation so they will be banned
    // @todo: DIRTY!
    $bannedCallbacks = ['text_feeds_set_target'];
    if (isset($targets[$target]['callback']) && !in_array($targets[$target]['callback'], $bannedCallbacks)) {
      //dsm("CALLING CALLBACK($target): " . $targets[$target]['callback']);
      // All target callbacks expect an array.
      $value = is_array($value) ? $value : [$value];
      call_user_func($targets[$target]['callback'], $source, $target_item, $target, $value, $mapping);
    } else {
      $this->setTranslatableTargetElement($source, $target_item, $target, $value);
    }
  }


  /**
   * @param \FeedsSource $source
   * @param \stdClass    $target_item
   * @param string       $target_element
   * @param mixed        $value
   */
  public function setTranslatableTargetElement(FeedsSource $source, $target_item, $target_element, $value) {
    $translatable = $this->isTranslatableField($target_element);
    //dsm("Mapping($target_element)-> translatable:" .  ($translatable?"Y":"N"));

    // Title must always be set because it was unset by map method
    if($target_element == 'title') {
      $target_item->$target_element = $value;
      return;
    }

    //Untranslatable fields can only be set when source is the same language as the entity's default language
    //@todo: make this an option
    if ($translatable || $this->currentItemLanguageKey == $target_item->language) {
      $languageKey = $translatable ? $this->currentItemLanguageKey : $this->neutralLanguageKey;
      // Let me spell this out...
      if($target_element == 'title') {
        $translationValue = $value;
      } else {
        $translationValue = [$languageKey => [['value' => $value]]];
      }
      $target_item->$target_element = $translationValue;
    } else {
      //dsm("Field(($target_element)-lang:[".$target_item->language."]) is skipped for entity language: " . $this->currentItemLanguageKey);
      unset($target_item->$target_element);
    }
  }

  /**
   * Override parent::configDefaults().
   */
  public function configDefaults() {
    return array_merge(parent::configDefaults(), [
                                                 'source_language_key' => 'language',
                                               ]
    );
  }

  /**
   * @param $form_state
   * @return array
   */
  public function configForm(&$form_state) {
    $form = parent::configForm($form_state);

    $form['source_language_key'] = [
      '#type' => 'textfield',
      '#title' => t('Source Language Column'),
      '#description' => t(
        'The name of the column you are using in your source to identify for which language to import the data. '
        . '<br />The currently available languages on your site are: '
      ) . '(' . implode("|", array_keys($this->languages)) . ')',
      '#default_value' => $this->config['source_language_key'],
    ];

    return $form;
  }


  /**
   * @param string $fieldName
   * @return bool
   */
  protected function isTranslatableField($fieldName) {
    $answer = false;
    if($fieldName) {
      if(!isset($this->fieldInfo[$fieldName])) {
        $fi = field_info_field($fieldName);
        if($fi && isset($fi["translatable"])) {
          $this->fieldInfo[$fieldName] = $fi["translatable"];
        } else {
          $this->fieldInfo[$fieldName] = false;
        }
      }
      $answer = $this->fieldInfo[$fieldName];
    }
    return $answer;
  }

  /**
   * @todo: implement keys: 'xx-et-default', 'xx-et-current', 'xx-et-author'
   */
  protected function setAvailableLanguages() {
    $list = language_list('enabled');
    foreach($list as $langList) {
      foreach($langList as $lang) {
        $this->languages[$lang->language] = $lang->name;
      }
    }
  }

  /**
   * @param string $type
   * @return string
   */
  protected function getDefaultLanguageForNodeType($type) {
    $answer = $this->neutralLanguageKey;
    $key = 'entity_translation_settings_node__' . $type;
    $ETNodeSettings = variable_get($key);
    if($ETNodeSettings
       && isset($ETNodeSettings['default_language'])
       && in_array($ETNodeSettings['default_language'], array_keys($this->languages))) {
      $answer = $ETNodeSettings['default_language'];
    }
    return $answer;
  }

  /**
   * @param $nids
   */
  protected function entityDeleteMultiple($nids) {
    parent::entityDeleteMultiple($nids);
  }
}